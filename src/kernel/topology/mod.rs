use core::{cell::SyncUnsafeCell, mem::MaybeUninit};

use alloc::boxed::Box;

use crate::kernel::memory::kmalloc::Kmalloc;

static CPU_REGISTRY: SyncUnsafeCell<MaybeUninit<CpuRegistry>> =
    SyncUnsafeCell::new(MaybeUninit::uninit());

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
}

impl CpuRegistry {
    pub fn register<T>(cpus: &[T], bsp: CpuHardwareId, f: impl Fn(&T) -> CpuHardwareId) -> Self {
        let mut registry = Box::new_uninit_slice_in(cpus.len(), Kmalloc::default());

        for i in 0..registry.len() {
            let hardware_id = f(&cpus[i]);

            if hardware_id == bsp {
                registry[0].write(Cpu {
                    id: CpuId::new(0),
                    hardware_id,
                });
            } else {
                registry[i + 1].write(Cpu {
                    id: CpuId::new((i + 1) as u32),
                    hardware_id,
                });
            }
        }

        Self {
            cpus: unsafe { registry.assume_init() },
        }
    }

    pub fn get<'a>() -> &'a Self {
        unsafe { (*CPU_REGISTRY.get()).assume_init_ref() }
    }

    pub const fn bsp_id(&self) -> CpuId {
        CpuId(0)
    }
}
