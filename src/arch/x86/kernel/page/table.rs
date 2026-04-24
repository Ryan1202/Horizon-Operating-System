use core::{
    ops::{Index, IndexMut},
    sync::atomic::AtomicUsize,
};

use crate::{
    arch::PhysAddr,
    kernel::memory::{
        KERNEL_BASE,
        arch::ArchMemory,
        frame::FrameNumber,
        page::{PageTable, linear_table_ptr},
    },
};

use super::entry::{X86EntryInfo, X86PageEntry};

pub const PDE_SHIFT: usize = 10;

const PAGE_OFFSET_BIT: usize = 12;
pub(super) const INDEX_BITS: [usize; 4] = [9, 9, 9, 9];
pub(super) const LEVEL_SHIFTS: [usize; 5] = [
    0,
    INDEX_BITS[0],
    INDEX_BITS[0] + INDEX_BITS[1],
    INDEX_BITS[0] + INDEX_BITS[1] + INDEX_BITS[2],
    INDEX_BITS[0] + INDEX_BITS[1] + INDEX_BITS[2] + INDEX_BITS[3],
];
pub(super) const LEVEL_COUNTS: [usize; 5] = [
    1 << LEVEL_SHIFTS[0],
    1 << LEVEL_SHIFTS[1],
    1 << LEVEL_SHIFTS[2],
    1 << LEVEL_SHIFTS[3],
    1 << LEVEL_SHIFTS[4],
];
pub(super) const LEVEL_MASKS: [usize; 5] = [
    !(LEVEL_COUNTS[0] - 1),
    !(LEVEL_COUNTS[1] - 1),
    !(LEVEL_COUNTS[2] - 1),
    !(LEVEL_COUNTS[3] - 1),
    !(LEVEL_COUNTS[4] - 1),
];
pub(super) const HUGE_PAGE: [bool; 4] = [false, true, true, false];

pub struct X86PageTable {
    table: [X86PageEntry; Self::PAGE_SIZE / size_of::<AtomicUsize>()],
}

impl Index<usize> for X86PageTable {
    type Output = X86PageEntry;

    fn index(&self, index: usize) -> &Self::Output {
        debug_assert!(index < Self::PAGE_SIZE / size_of::<AtomicUsize>());
        &self.table[index]
    }
}

impl IndexMut<usize> for X86PageTable {
    fn index_mut(&mut self, index: usize) -> &mut Self::Output {
        debug_assert!(index < Self::PAGE_SIZE / size_of::<AtomicUsize>());
        &mut self.table[index]
    }
}

impl X86PageTable {
    pub const fn kernel_table_ptr<T>(frame: FrameNumber) -> *const T {
        let kernel_start_phy = 0;
        let kernel_start = KERNEL_BASE;
        let phys = PhysAddr::from_frame_number(frame).as_usize();

        (kernel_start + (phys - kernel_start_phy)).as_ptr()
    }

    pub const fn linear_table_ptr<T>(frame: FrameNumber) -> *const T {
        linear_table_ptr::<Self>(frame) as *const T
    }
}

impl PageTable for X86PageTable {
    type Entry = X86EntryInfo;
    type EntrySlot = X86PageEntry;

    const PAGE_OFFSET_BITS: usize = PAGE_OFFSET_BIT;
    const LEVELS: usize = 4;
    const INDEX_BITS: &'static [usize] = &INDEX_BITS;
    const LEVEL_SHIFTS: &'static [usize] = &LEVEL_SHIFTS;
    const LEVEL_COUNTS: &'static [usize] = &LEVEL_COUNTS;
    const LEVEL_MASKS: &'static [usize] = &LEVEL_MASKS;
    const HUGE_PAGE: &'static [bool] = &HUGE_PAGE;
}
