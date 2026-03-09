use crate::kernel::memory::page::PageNumber;

pub trait FlushTlb {
    /// 刷新单个虚拟页的 TLB 条目
    fn flush_page(page_number: PageNumber);

    /// 刷新一个虚拟地址范围 [start, start + count * PAGE_SIZE) 的 TLB 条目
    fn flush_range(start: PageNumber, end: PageNumber);

    // 刷新所有 TLB 条目
    fn flush_all();

    // 刷新所有 TLB 条目，包括 Global
    fn flush_all_inclusive_global();
}
