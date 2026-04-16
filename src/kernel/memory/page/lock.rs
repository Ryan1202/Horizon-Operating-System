use core::{
    marker::PhantomData,
    ops::{Deref, DerefMut},
    ptr::NonNull,
};

use crate::{
    arch::{ArchPageTable, PhysAddr},
    kernel::memory::{
        KLINEAR_BASE,
        frame::{Frame, FrameNumber},
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

    pub fn from(ptr: *const T) -> Self {
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

    fn read_lock(page: PtPage<T>) -> Self::ReadGuard;
}

pub struct NormalPtLock;
pub struct EarlyPtLock;

impl<'a> PtRwLock<'a, ArchPageTable> for NormalPtLock {
    type ReadGuard = RwReadGuard<'a, ArchPageTable>;
    type WriteGuard = RwWriteGuard<'a, ArchPageTable>;

    fn read_lock(page: PtPage<ArchPageTable>) -> Self::ReadGuard {
        let frame = page.frame;
        let frame = Frame::get_raw(frame);
        unsafe { frame.as_ref().get_data().page_table.read_lock(page.ptr) }
    }
}

pub struct EarlyReadPage<'a, T: PageTable> {
    table: NonNull<T>,
    _phantom: PhantomData<&'a T>,
}

impl<'a, T: PageTable> Deref for EarlyReadPage<'a, T> {
    type Target = T;

    fn deref(&self) -> &'a Self::Target {
        unsafe { self.table.as_ref() }
    }
}

impl<'a, T: PageTable> From<EarlyReadPage<'a, T>> for EarlyWritePage<'a, T> {
    fn from(value: EarlyReadPage<'a, T>) -> Self {
        Self {
            table: value.table,
            _phantom: PhantomData,
        }
    }
}

impl<'a, T: PageTable> From<EarlyWritePage<'a, T>> for EarlyReadPage<'a, T> {
    fn from(value: EarlyWritePage<'a, T>) -> Self {
        Self {
            table: value.table,
            _phantom: PhantomData,
        }
    }
}

pub struct EarlyWritePage<'a, T: PageTable> {
    table: NonNull<T>,
    _phantom: PhantomData<&'a mut T>,
}

impl<'a, T: PageTable> Deref for EarlyWritePage<'a, T> {
    type Target = T;

    fn deref(&self) -> &'a Self::Target {
        unsafe { self.table.as_ref() }
    }
}

impl<'a, T: PageTable> DerefMut for EarlyWritePage<'a, T> {
    fn deref_mut(&mut self) -> &'a mut Self::Target {
        unsafe { self.table.as_mut() }
    }
}

impl<'a, T: PageTable + 'a> PtRwLock<'a, T> for EarlyPtLock {
    type ReadGuard = EarlyReadPage<'a, T>;
    type WriteGuard = EarlyWritePage<'a, T>;

    fn read_lock(page: PtPage<T>) -> Self::ReadGuard {
        EarlyReadPage {
            table: page.ptr,
            _phantom: PhantomData,
        }
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
        let top_level = T::top_level();
        let mut read_guards = [None, None, None, None, None];
        read_guards[top_level] = Some(root_guard);

        let mut page_lock = Self {
            pt_base: page,
            current_level: Some(top_level),
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

            page_lock.read_guards[child_level] = Some(Lock::read_lock(child_page));
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

    pub fn push(&mut self, index: usize) -> Option<()> {
        let level = self.current_level?;
        if level == 0 {
            return None;
        }

        self.restore_lock_state();

        let entry = self.get()?.get_entry(index).read();

        let frame = entry.frame_number()?;

        if entry.is_huge(level as u8) {
            return None;
        }

        let child_level = level - 1;
        let child_page = PtPage::new((self.phy2vir)(frame), frame);

        assert!(
            self.read_guards[child_level].is_none(),
            "page lock stack corruption: child level already occupied"
        );

        let mut target_page = self.pt_base.get();
        target_page &= T::LEVEL_MASKS[level + 1];
        target_page |= index << T::LEVEL_SHIFTS[level];
        self.pt_base = PageNumber::new(target_page);

        self.read_guards[child_level] = Some(Lock::read_lock(child_page));
        self.current_level = Some(child_level);

        Some(())
    }

    pub fn pop(&mut self) -> Option<()> {
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

        Some(())
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

impl<'a, T, Lock> Drop for PageLock<'a, T, Lock>
where
    T: PageTable + 'a,
    Lock: PtRwLock<'a, T>,
{
    fn drop(&mut self) {
        let _ = self.write_guard.take();

        let start = self.current_level.unwrap_or(self.read_guards.len());
        for level in start..self.read_guards.len() {
            let _ = self.read_guards[level].take();
        }
    }
}
