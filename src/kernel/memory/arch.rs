use crate::kernel::memory::page::PageEntry;

pub trait ArchMemory {
    const PAGE_BITS: usize;
    const PAGE_SIZE: usize = 1 << Self::PAGE_BITS;

    const TOTAL_ENTRIES: usize = 1 << (Self::VIRT_ADDR_BITS - Self::PAGE_BITS);

    /// 页表层级数。
    /// - x86: 2（PD → PT）
    /// - x86_64: 4（PML4 → PDPT → PD → PT）
    /// - aarch64: 3 或 4（取决于配置）
    const PAGE_TABLE_LEVELS: usize;

    /// 虚拟地址有效位数。
    /// - x86: 32
    /// - x86_64: 48（或 57 with LA57）
    /// - aarch64: 48
    const VIRT_ADDR_BITS: usize;

    /// 物理地址有效位数。
    /// - x86: 32（或 36 with PAE）
    /// - x86_64: 52
    /// - aarch64: 48
    const PHYS_ADDR_BITS: usize;

    /// 该架构的页表条目类型
    type Entry: PageEntry;
}
