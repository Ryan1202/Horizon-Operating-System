use crate::{
    arch::{ArchPageTable, PhysAddr, VirtAddr},
    kernel::memory::{
        arch::ArchMemory,
        page::{PageTableOps, current_root_pt},
    },
};

pub fn translate(vaddr_base: VirtAddr, size: usize) -> Option<PhysAddr> {
    PhysAddr::try_from_linear_virt(vaddr_base + size)
        .and(PhysAddr::try_from_linear_virt(vaddr_base))
        .or_else(|| {
            let root_pt = current_root_pt();

            let mut vaddr = vaddr_base;
            let paddr = PageTableOps::<ArchPageTable>::translate(root_pt, vaddr)?;
            let end = vaddr + size;
            while vaddr < end {
                let tmp = PageTableOps::<ArchPageTable>::translate(root_pt, vaddr)?;

                // 检查物理地址是否连续
                if tmp - paddr != vaddr - vaddr_base {
                    return None;
                }

                let step = (ArchPageTable::PAGE_SIZE - vaddr.page_offset()).min(end - vaddr);
                vaddr += step;
            }
            Some(paddr)
        })
}
