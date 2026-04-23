use crate::kernel::memory::{
    frame::FrameNumber,
    page::{PageNumber, PageTable, PageTableEntry, PageTableEntrySlot},
};

use super::lock::{PageLock, PtPage, PtRwLock};

#[derive(Clone, Copy)]
pub enum PtStep {
    Absent {
        page: PageNumber,
        level: usize,
    },
    Table {
        /// 当前页表内的起始页号
        base: PageNumber,
        /// 页表页的物理页号
        frame: FrameNumber,
        level: usize,
    },
    Leaf {
        page: PageNumber,
        frame: FrameNumber,
        level: usize,
    },
}

pub struct PageTableIter<'a, T, L>
where
    T: PageTable + 'a,
    L: PtRwLock<'a, T>,
{
    lock: PageLock<'a, T, L>,
    index: usize,
    end: PageNumber,
}

impl<'a, T, L> PageTableIter<'a, T, L>
where
    T: PageTable + 'a,
    L: PtRwLock<'a, T>,
{
    pub fn new(
        root: PtPage<T>,
        page_start: PageNumber,
        page_end: PageNumber,
        phy2vir: fn(FrameNumber) -> *const T,
    ) -> Option<Self> {
        let lock = PageLock::lock_page(root, page_start, phy2vir)?;
        let level = lock.level()?;
        let index = T::entry_index(page_start, level);

        Some(Self {
            lock,
            index,
            end: page_end,
        })
    }

    pub fn get_mut(&mut self) -> Option<&mut T> {
        self.lock.get_mut()
    }

    pub fn get_lock(&self) -> &PageLock<'a, T, L> {
        &self.lock
    }
}

impl<'a, T, L> Iterator for PageTableIter<'a, T, L>
where
    T: PageTable + 'a,
    L: PtRwLock<'a, T>,
{
    type Item = PtStep;

    fn next(&mut self) -> Option<Self::Item> {
        let mut base = self.lock.base();
        let mut level = self.lock.level()?;

        if self.index >= 1 << T::INDEX_BITS[self.lock.level()?] {
            let (_, level) = self.lock.pop()?;

            let page = base + (self.index << T::LEVEL_SHIFTS[level]);
            self.index = T::entry_index(page, level);

            let entry = self.lock.get()?.get_entry(self.index).read();
            let frame = entry.frame_number()?;

            self.index += 1;

            return Some(PtStep::Table { base, frame, level });
        }

        loop {
            let page = base + (self.index << T::LEVEL_SHIFTS[level]);
            if page >= self.end {
                return None;
            }

            let entry_ptr = self.lock.get()?.get_entry(self.index);
            let entry = entry_ptr.read();
            self.index += 1;

            let Some(frame) = entry.frame_number() else {
                return Some(PtStep::Absent { page, level });
            };
            if !entry.is_present() {
                return Some(PtStep::Absent { page, level });
            }

            if level == 0 || entry.is_huge(level as u8) {
                return Some(PtStep::Leaf { page, frame, level });
            }

            self.lock.push(self.index - 1, None)?;
            self.index = 0;

            base = self.lock.base();
            level = self.lock.level()?;
        }
    }
}
