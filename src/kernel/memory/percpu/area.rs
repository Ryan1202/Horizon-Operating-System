use core::{alloc::Layout, cell::SyncUnsafeCell, mem::MaybeUninit};

use crate::{
    CACHELINE_SIZE,
    kernel::{
        memory::{
            MemoryError,
            frame::reference::UniqueFrames,
            page::Pages,
            percpu::{__percpu_start, PerCpuDynHandle, chunk::PerCpuChunk, percpu_template_size},
        },
        topology::CpuId,
    },
};

pub(super) const ALLOC_UNIT: usize = 8;

pub(super) static PERCPU_AREA: SyncUnsafeCell<MaybeUninit<PercpuArea>> =
    SyncUnsafeCell::new(MaybeUninit::uninit());

pub struct PercpuArea {
    first_chunk: PerCpuChunk,
    /// 当前 per-CPU 区域的 CPU 核心数量
    count: u32,
    /// 每个 CPU unit 的实际大小，单位为字节
    unit_size: usize,
}

// SAFETY: PERCPU_AREA 在启动阶段一次性初始化；其持有的 frame 在内核生命周期
// 内不会释放，后续只通过不可变引用查询各 CPU 的区域地址。
unsafe impl Sync for PercpuArea {}

impl PercpuArea {
    pub fn try_new(frame: UniqueFrames, nr_cpu_limit: usize) -> Result<Self, MemoryError> {
        if nr_cpu_limit == 0 {
            return Err(MemoryError::ViolateConstraint);
        }

        let addr = frame.start_addr().try_to_virt().unwrap();

        let template_size = percpu_template_size();
        let dynamic_start = template_size.next_multiple_of(CACHELINE_SIZE);

        // 根据实际分配大小计算每个 CPU unit 的大小
        let unit_size = (frame.order().to_size() / nr_cpu_limit) & !(CACHELINE_SIZE - 1);

        let start = &raw const __percpu_start;

        for i in 0..nr_cpu_limit {
            let dest = unsafe { addr.as_mut_ptr::<u8>().add(i * unit_size) };

            // SAFETY: 每个目标 unit 位于独立且已分配的 backing 中，模板范围由链接器保证有效
            unsafe { start.copy_to_nonoverlapping(dest, template_size) };
        }

        let first_chunk = PerCpuChunk::try_new(Pages::Linear(frame), dynamic_start, unit_size)?;
        Ok(Self {
            first_chunk,
            count: nr_cpu_limit as u32,
            unit_size,
        })
    }

    pub fn index(&self, cpu_id: CpuId) -> *mut u8 {
        assert!(cpu_id.get() < self.count);

        let start: *mut u8 = self.first_chunk.get_mut_ptr();
        unsafe { start.byte_add(cpu_id.get() as usize * self.unit_size) }
    }

    pub const fn count(&self) -> u32 {
        self.count
    }

    /// 分配动态 per-CPU 内存
    pub fn allocate(&self, layout: Layout) -> Result<PerCpuDynHandle, MemoryError> {
        self.first_chunk.allocate(layout).map(PerCpuDynHandle::new)
    }

    /// 释放动态分配的 per-CPU 内存
    ///
    pub fn deallocate(&self, dyn_percpu: &PerCpuDynHandle) -> Result<(), MemoryError> {
        let dynamic_position = dyn_percpu.dynamic_position();

        self.first_chunk.deallocate(dynamic_position)
    }
}
