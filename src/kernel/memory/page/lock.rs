use core::{
    marker::PhantomData,
    ops::{Deref, DerefMut},
    ptr::NonNull,
};

use crate::{
    arch::{ArchPageTable, PhysAddr},
    kernel::memory::{
        KLINEAR_BASE,
        frame::{Frame, FrameNumber, reference::SharedFrames},
        page::{PageNumber, PageTable, PageTableEntry, PageTableEntrySlot},
    },
    lib::rust::spinlock::{RwReadGuard, RwWriteGuard},
};

const MAX_PAGE_TABLE_LEVELS: usize = 5;

pub struct PtPage<T: PageTable> {
    ptr: NonNull<T>,
    frame: FrameNumber,
}

impl<T: PageTable> Clone for PtPage<T> {
    fn clone(&self) -> Self {
        *self
    }
}

impl<T: PageTable> Copy for PtPage<T> {}

impl<T: PageTable> PtPage<T> {
    pub fn new(ptr: *const T, frame: FrameNumber) -> Self {
        Self {
            ptr: NonNull::new(ptr as *mut T).expect("page table pointer is null"),
            frame,
        }
    }

    pub fn from_ptr(ptr: *const T) -> Self {
        let addr = ptr.addr();
        assert!(
            addr >= KLINEAR_BASE.as_usize(),
            "runtime page table must be linear mapped"
        );
        let frame = PhysAddr::new(addr - KLINEAR_BASE.as_usize()).to_frame_number();
        Self::new(ptr, frame)
    }
}

pub trait PtRwLock<'a, T: PageTable> {
    type ReadGuard: 'a + Deref<Target = T> + Into<Self::WriteGuard>;
    type WriteGuard: 'a + Deref<Target = T> + DerefMut<Target = T> + Into<Self::ReadGuard>;
    type Table: Clone;

    fn read_lock(page: PtPage<T>) -> Self::ReadGuard;
    fn table(page: PtPage<T>) -> Option<Self::Table>;
    unsafe fn drop_table(table: &mut Self::Table) -> Option<usize>;
}

pub struct NormalPtLock;
pub struct EarlyPtLock;

impl<'a> PtRwLock<'a, ArchPageTable> for NormalPtLock {
    type ReadGuard = RwReadGuard<'a, ArchPageTable>;
    type WriteGuard = RwWriteGuard<'a, ArchPageTable>;
    type Table = SharedFrames;

    fn read_lock(page: PtPage<ArchPageTable>) -> Self::ReadGuard {
        let frame = page.frame;
        let frame = Frame::get_raw(frame);
        unsafe {
            frame
                .as_ref()
                .get_data()
                .page_table
                .lock
                .read_lock(page.ptr)
        }
    }

    fn table(page: PtPage<ArchPageTable>) -> Option<Self::Table> {
        SharedFrames::new(Frame::get_raw(page.frame))
    }

    unsafe fn drop_table(table: &mut Self::Table) -> Option<usize> {
        table.refcount.release()
    }
}

pub struct EarlyRwPage<'a, T: PageTable> {
    table: NonNull<T>,
    _phantom: PhantomData<&'a mut T>,
}

impl<'a, T: PageTable> Deref for EarlyRwPage<'a, T> {
    type Target = T;

    fn deref(&self) -> &'a Self::Target {
        unsafe { self.table.as_ref() }
    }
}

impl<'a, T: PageTable> DerefMut for EarlyRwPage<'a, T> {
    fn deref_mut(&mut self) -> &'a mut Self::Target {
        unsafe { self.table.as_mut() }
    }
}

impl<'a, T: PageTable + 'a> PtRwLock<'a, T> for EarlyPtLock {
    type ReadGuard = EarlyRwPage<'a, T>;
    type WriteGuard = EarlyRwPage<'a, T>;
    type Table = ();

    fn read_lock(page: PtPage<T>) -> Self::ReadGuard {
        EarlyRwPage {
            table: page.ptr,
            _phantom: PhantomData,
        }
    }

    fn table(_: PtPage<T>) -> Option<Self::Table> {
        Some(())
    }

    unsafe fn drop_table(_: &mut Self::Table) -> Option<usize> {
        Some(1)
    }
}

pub struct PageLock<'a, T, Lock>
where
    T: PageTable + 'a,
    Lock: PtRwLock<'a, T>,
{
    /// 当前页表的起始页号
    pt_base: PageNumber,
    current_level: Option<usize>,

    refs: [Option<Lock::Table>; MAX_PAGE_TABLE_LEVELS],
    read_guards: [Option<Lock::ReadGuard>; MAX_PAGE_TABLE_LEVELS],
    write_guard: Option<Lock::WriteGuard>,

    phy2vir: fn(FrameNumber) -> *const T,
    _phantom: PhantomData<&'a (T, Lock)>,
}

impl<'a, T, Lock> PageLock<'a, T, Lock>
where
    T: PageTable + 'a,
    Lock: PtRwLock<'a, T>,
{
    pub fn lock_page(
        root: PtPage<T>,
        page: PageNumber,
        phy2vir: fn(FrameNumber) -> *const T,
    ) -> Option<Self> {
        assert!(T::LEVELS <= MAX_PAGE_TABLE_LEVELS);

        let root_guard = Lock::read_lock(root);
        let root_ref = Lock::table(root)?;

        let top_level = T::top_level();
        let mut read_guards = [None, None, None, None, None];
        let mut refs = [None, None, None, None, None];
        read_guards[top_level] = Some(root_guard);
        refs[top_level] = Some(root_ref);

        let mut page_lock = Self {
            pt_base: page,
            current_level: Some(top_level),
            refs,
            read_guards,
            write_guard: None,
            phy2vir,
            _phantom: PhantomData,
        };

        while let Some(level) = page_lock.current_level
            && level > 0
        {
            let index = T::entry_index(page, level);
            let entry = page_lock.get()?.get_entry(index).read();

            if !entry.is_present() || entry.is_huge(level as u8) {
                break;
            }

            let child_level = level - 1;
            let frame = entry.frame_number()?;

            let ptr = (page_lock.phy2vir)(frame);
            let child_page = PtPage::new(ptr, frame);

            let guard = Lock::read_lock(child_page);
            let frame = Lock::table(child_page)?;

            page_lock.refs[child_level] = Some(frame);
            page_lock.read_guards[child_level] = Some(guard);
            page_lock.current_level = Some(child_level);
        }

        let pt_base = page_lock.pt_base.get() & T::LEVEL_MASKS[page_lock.current_level? + 1];
        page_lock.pt_base = PageNumber::new(pt_base);

        Some(page_lock)
    }

    pub const fn level(&self) -> Option<usize> {
        self.current_level
    }

    pub const fn base(&self) -> PageNumber {
        self.pt_base
    }

    pub fn get(&self) -> Option<&T> {
        self.write_guard.as_deref().or_else(|| {
            let level = self.current_level?;
            self.read_guards[level].as_deref()
        })
    }

    pub fn get_mut(&mut self) -> Option<&mut T> {
        let level = self.current_level?;

        if self.write_guard.is_none() {
            let read_guard = self.read_guards[level].take()?;
            self.write_guard = Some(read_guard.into());
        }

        self.write_guard.as_deref_mut()
    }

    /// 克隆当前页表页引用
    pub fn clone_current(&self) -> Option<Lock::Table> {
        let level = self.current_level?;
        self.refs[level].clone()
    }

    /// 减少当前页表页引用计数，返回剩余引用计数
    ///
    /// # Safety
    ///
    /// 需要自行确保增加和释放对称
    pub unsafe fn drop_current(&mut self) -> Option<usize> {
        let level = self.current_level?;
        unsafe { Lock::drop_table(self.refs[level].as_mut()?) }
    }

    /// 进入子页表
    ///
    /// `new_table` 用于传入新创建的页表引用，如果为 `None` 则自动创建
    pub fn push(&mut self, index: usize, new_table: Option<Lock::Table>) -> Option<()> {
        let level = self.current_level?;
        if level == 0 {
            return None;
        }

        self.restore_lock_state();

        let entry = self.get()?.get_entry(index).read();

        let frame_number = entry.frame_number()?;

        if entry.is_huge(level as u8) {
            return None;
        }

        let child_level = level - 1;
        let child_page = PtPage::new((self.phy2vir)(frame_number), frame_number);

        assert!(
            self.read_guards[child_level].is_none(),
            "page lock stack corruption: child level already occupied"
        );

        let mut target_page = self.pt_base.get();
        target_page &= T::LEVEL_MASKS[level + 1];
        target_page |= index << T::LEVEL_SHIFTS[level];
        self.pt_base = PageNumber::new(target_page);

        let table = new_table.or_else(|| Lock::table(child_page));
        self.refs[child_level] = table;
        self.read_guards[child_level] = Some(Lock::read_lock(child_page));
        self.current_level = Some(child_level);

        Some(())
    }

    /// 退出当前页表
    pub fn pop(&mut self) -> Option<(Lock::Table, usize)> {
        let level = self.current_level?;

        if self.write_guard.is_some() {
            self.write_guard.take().map(drop)
        } else {
            self.read_guards[level].take().map(drop)
        }?;

        if level == T::top_level() {
            self.current_level = None;
        } else {
            self.current_level = Some(level + 1);
            self.pt_base = PageNumber::new(self.pt_base.get() & T::LEVEL_MASKS[level + 2]);
        }

        self.refs[level].take().zip(self.current_level)
    }

    pub fn restore_lock_state(&mut self) {
        let Some(level) = self.current_level else {
            return;
        };
        let Some(write_guard) = self.write_guard.take() else {
            return;
        };

        self.read_guards[level] = Some(write_guard.into());
    }
}

impl<'a> PageLock<'a, ArchPageTable, NormalPtLock> {
    pub fn get_frame(&mut self) -> Option<&mut SharedFrames> {
        self.refs[self.current_level?].as_mut()
    }
}

impl<'a, T, Lock> Drop for PageLock<'a, T, Lock>
where
    T: PageTable + 'a,
    Lock: PtRwLock<'a, T>,
{
    fn drop(&mut self) {
        let _ = self.write_guard.take();

        let start = self.current_level.unwrap_or(0);
        for level in start..self.read_guards.len() {
            let _ = self.read_guards[level].take();
        }
    }
}
