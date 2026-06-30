use crate::kernel::memory::{
    MemoryError,
    dma::{
        Direction,
        scatter_gather::EntryList,
        source::{BouncePool, software_iotlb::BounceAddr},
        sync::bounce::BounceSync,
    },
};

pub struct ScatterGatherSync;

impl ScatterGatherSync {
    pub fn sync(
        entries: &mut EntryList,
        n_entries: usize,
        direction: Direction,
    ) -> Result<(), MemoryError> {
        let iter = entries.iter_mut();

        let pool = BouncePool::get_pool();
        for entry in iter.take(n_entries) {
            let paddr = entry.phys_addr().ok_or(MemoryError::UnavailableFrame)?;

            if let Some(addr) = BounceAddr::new(paddr) {
                // 如果当前 EntryList 的原物理地址在 Bounce Pool 中，说明需要进行同步
                let slot = pool.clone_slot(addr);
                BounceSync::sync(addr, &slot, entry.size(), direction)?;
            }
            // 原物理地址没有对应 bounce slot，说明它是直接映射的。
        }

        Ok(())
    }
}
