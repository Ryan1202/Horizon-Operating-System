use core::{
    iter::once,
    mem::{MaybeUninit, offset_of},
    sync::atomic::{AtomicUsize, Ordering},
};

use alloc::boxed::Box;

use crate::{
    kernel::memory::{kmalloc::Kmalloc, percpu::PerCpuInit},
    lib::rust::spinlock::Spinlock,
};

mod mask;

pub use mask::{CpuMask, ThinCpuMask};

/// 允许的最大 CPU 数量
pub const NR_CPUS_MAX: u32 = 64;

pub static CPU_REGISTRY: Spinlock<CpuRegistry> = Spinlock::new(CpuRegistry::new());
pub static CPU_LIMIT: AtomicUsize = AtomicUsize::new(NR_CPUS_MAX as usize);

#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(transparent)]
pub struct CpuId(u32);

unsafe impl PerCpuInit for CpuId {}

#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(transparent)]
pub struct CpuHardwareId(u32);

impl CpuId {
    pub const fn new(id: u32) -> Self {
        Self(id)
    }

    pub const fn get(&self) -> u32 {
        self.0
    }

    pub const fn bsp() -> Self {
        Self(0)
    }

    pub const fn uninited() -> Self {
        Self(u32::MAX)
    }

    /// 真正的整数 id 在 `CpuId` 中的偏移地址
    pub const fn id_offset() -> usize {
        offset_of!(Self, 0)
    }
}

impl CpuHardwareId {
    pub fn new(id: u32) -> Self {
        Self(id)
    }

    pub fn get_raw(&self) -> u32 {
        self.0
    }
}

impl Into<u32> for CpuHardwareId {
    fn into(self) -> u32 {
        self.0
    }
}

pub struct Cpu {
    id: CpuId,
    hardware_id: CpuHardwareId,
}

pub struct CpuRegistry {
    cpus: MaybeUninit<Box<[Cpu], Kmalloc>>,
    online: ThinCpuMask,
    possible: ThinCpuMask,
}

impl CpuRegistry {
    const fn new() -> Self {
        Self {
            cpus: MaybeUninit::uninit(),
            online: ThinCpuMask::mask_of(CpuId::bsp()),
            possible: ThinCpuMask::mask_of(CpuId::bsp()),
        }
    }

    pub fn init<T>(cpu_ids: &[T], bsp: CpuHardwareId, f: impl Fn(&T) -> Option<CpuHardwareId>) {
        let count = cpu_ids.iter().filter_map(&f).count().min(get_cpu_limit());
        let mut cpus = Box::new_uninit_slice_in(count, Kmalloc::default());

        // 固定 BSP 为逻辑 CPU0，其他 CPU 连续编号，与 MADT 中的排列无关。
        let ids = once(bsp).chain(cpu_ids.iter().filter_map(&f).filter(|id| *id != bsp));
        for (i, hardware_id) in ids.take(count).enumerate() {
            cpus[i].write(Cpu {
                id: CpuId::new(i as u32),
                hardware_id,
            });
        }

        unsafe {
            CPU_REGISTRY.lock().cpus = MaybeUninit::new(cpus.assume_init());
        };
    }

    pub const fn bsp_id() -> CpuId {
        CpuId::bsp()
    }

    pub fn online_cpus(&self) -> ThinCpuMask {
        self.online.clone()
    }

    pub fn hardware_id(&self, cpu: CpuId) -> Option<CpuHardwareId> {
        unsafe {
            self.cpus
                .assume_init_ref()
                .get(cpu.get() as usize)
                .map(|entry| entry.hardware_id)
        }
    }

    /// 仅在该 CPU 的中断入口和控制器就绪后发布；暂不支持 CPU 下线。
    pub(crate) fn mark_online(&mut self, cpu: CpuId) {
        debug_assert!(self.hardware_id(cpu).is_some());
        self.online.set(cpu);
    }

    pub const fn iter_possible<'a>(&'a self) -> CpuIter<'a> {
        CpuIter {
            mask: &self.possible,
            index: 0,
        }
    }
}

pub struct CpuIter<'a> {
    mask: &'a ThinCpuMask,
    index: u32,
}

impl<'a> Iterator for CpuIter<'a> {
    type Item = CpuId;

    fn next(&mut self) -> Option<Self::Item> {
        let cpu = self.mask.find_next(self.index)?;
        self.index = cpu.get() + 1;
        Some(cpu)
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn set_cpu_limit(limit: usize) {
    CPU_LIMIT.store(limit, Ordering::Relaxed);
}

pub fn get_cpu_limit() -> usize {
    CPU_LIMIT.load(Ordering::Relaxed)
}
