use core::{cell::SyncUnsafeCell, mem::MaybeUninit, ptr::with_exposed_provenance};

use crate::kernel::memory::{
    frame::reference::UniqueFrames,
    percpu::{__percpu_start, percpu_stride, percpu_template_size},
};

pub(super) static PERCPU_AREA: SyncUnsafeCell<MaybeUninit<PercpuArea>> =
    SyncUnsafeCell::new(MaybeUninit::uninit());

pub struct PercpuArea {
    frame: UniqueFrames,
    /// 该区域中包含的 CPU 核心数量
    count: usize,
}

// SAFETY: PERCPU_AREA 在启动阶段一次性初始化；其持有的 frame 在内核生命周期
// 内不会释放，后续只通过不可变引用查询各 CPU 的区域地址。
unsafe impl Sync for PercpuArea {}

impl PercpuArea {
    pub fn new(frame: UniqueFrames, nr_cpus: usize) -> Self {
        assert!(nr_cpus > 0, "per-CPU area requires at least one CPU");

        let addr = frame.start_addr().try_to_virt().unwrap();

        let template_size = percpu_template_size();
        let stride = percpu_stride();
        let total_size = stride
            .checked_mul(nr_cpus)
            .expect("per-CPU area size overflow");
        assert!(
            frame.order().to_size() >= total_size,
            "allocated frames are smaller than the per-CPU area"
        );

        let start = with_exposed_provenance::<u8>((&raw const __percpu_start).addr());

        for i in 0..nr_cpus {
            let dest = unsafe { addr.as_mut_ptr::<u8>().add(i * stride) };
            unsafe {
                start.copy_to_nonoverlapping(dest, template_size);
            };
        }

        Self {
            frame,
            count: nr_cpus,
        }
    }

    pub fn index(&self, index: usize) -> *mut u8 {
        assert!(index < self.count);

        let start = self
            .frame
            .start_addr()
            .try_to_virt()
            .unwrap()
            .as_mut_ptr::<u8>();
        unsafe { start.add(index * percpu_stride()) }
    }
}
