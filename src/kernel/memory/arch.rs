pub trait ArchMemory {
    const PAGE_BITS: usize;
    const PAGE_SIZE: usize = 1 << Self::PAGE_BITS;

    const TOTAL_ENTRIES: usize = 1 << (Self::VIRT_ADDR_BITS - Self::PAGE_BITS);

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
}
