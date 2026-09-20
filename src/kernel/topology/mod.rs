use core::{
    cell::SyncUnsafeCell,
    iter::once,
    mem::MaybeUninit,
    sync::atomic::{AtomicUsize, Ordering},
};

use alloc::boxed::Box;

use crate::{kernel::memory::kmalloc::Kmalloc, lib::rust::spinlock::Spinlock};

mod mask;

pub use mask::{CpuMask, ThinCpuMask};

/// 允许的最大 CPU 数量
pub const NR_CPUS_MAX: u32 = 64;

static CPU_REGISTRY: SyncUnsafeCell<MaybeUninit<CpuRegistry>> =
    SyncUnsafeCell::new(MaybeUninit::uninit());
pub static CPU_LIMIT: AtomicUsize = AtomicUsize::new(NR_CPUS_MAX as usize);

#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(transparent)]
pub struct CpuId(u32);

#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(transparent)]
pub struct CpuHardwareId(u32);

impl CpuId {
    pub fn new(id: u32) -> Self {
        Self(id)
    }

    pub fn get(&self) -> u32 {
        self.0
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
    cpus: Box<[Cpu], Kmalloc>,
    online: Spinlock<Box<ThinCpuMask, Kmalloc>>,
}

impl CpuRegistry {
    pub fn register<T>(cpus: &[T], bsp: CpuHardwareId, f: impl Fn(&T) -> Option<CpuHardwareId>) {
        let count = cpus.iter().filter_map(&f).count().min(get_cpu_limit());
        let mut registry = Box::new_uninit_slice_in(count, Kmalloc::default());

        // 固定 BSP 为逻辑 CPU0，其他 CPU 连续编号，与 MADT 中的排列无关。
        let ids = once(bsp).chain(cpus.iter().filter_map(&f).filter(|id| *id != bsp));
        for (i, hardware_id) in ids.take(count).enumerate() {
            registry[i].write(Cpu {
                id: CpuId::new(i as u32),
                hardware_id,
            });
        }

        let mut online: Box<ThinCpuMask, Kmalloc> =
            unsafe { Box::new_zeroed_in(Kmalloc::default()).assume_init() };
        online.set(Self::bsp_id());

        unsafe {
            (*CPU_REGISTRY.get()).write(Self {
                cpus: registry.assume_init(),
                online: Spinlock::new(online),
            })
        };
    }

    pub fn get<'a>() -> &'a Self {
        unsafe { (*CPU_REGISTRY.get()).assume_init_ref() }
    }

    pub const fn bsp_id() -> CpuId {
        CpuId(0)
    }

    pub fn online_cpus(&self) -> ThinCpuMask {
        *self.online.lock_irqsave().clone()
    }

    pub fn hardware_id(&self, cpu: CpuId) -> Option<CpuHardwareId> {
        self.cpus
            .get(cpu.get() as usize)
            .map(|entry| entry.hardware_id)
    }

    /// 仅在该 CPU 的中断入口和控制器就绪后发布；暂不支持 CPU 下线。
    pub(crate) fn mark_online(&self, cpu: CpuId) {
        debug_assert!(self.hardware_id(cpu).is_some());
        self.online.lock_irqsave().set(cpu);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn set_cpu_limit(limit: usize) {
    CPU_LIMIT.store(limit, Ordering::Relaxed);
}

pub fn get_cpu_limit() -> usize {
    CPU_LIMIT.load(Ordering::Relaxed)
}
