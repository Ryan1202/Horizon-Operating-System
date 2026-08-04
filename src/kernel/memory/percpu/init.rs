use core::{
    mem::MaybeUninit,
    sync::atomic::{
        AtomicBool, AtomicI8, AtomicI16, AtomicI32, AtomicI64, AtomicIsize, AtomicU8, AtomicU16,
        AtomicU32, AtomicU64, AtomicUsize,
    },
};

use crate::{
    arch::ArchCpuLocal,
    kernel::memory::{
        frame::{FrameAllocator, buddy::FrameOrder, frame_manager, zone::ZoneType},
        percpu::{
            CpuLocal, PERCPU_DELTA, PerCpuReadWrite, PercpuArea, area::PERCPU_AREA, percpu_stride,
        },
    },
};

/// 可以由链接器模板按字节复制成多个独立 per-CPU 实例的类型。
pub unsafe trait PerCpuInit: Sized + 'static {}

macro_rules! impl_percpu_init {
    ($($t:ty),* $(,)?) => {
        $(unsafe impl PerCpuInit for $t {})*
    };
}

impl_percpu_init!(
    (),
    u8,
    u16,
    u32,
    u64,
    usize,
    i8,
    i16,
    i32,
    i64,
    isize,
    AtomicBool,
    AtomicI8,
    AtomicI16,
    AtomicI32,
    AtomicI64,
    AtomicIsize,
    AtomicU8,
    AtomicU16,
    AtomicU32,
    AtomicU64,
    AtomicUsize,
);

unsafe impl<T: PerCpuInit, const N: usize> PerCpuInit for [T; N] {}

pub fn percpu_init(nr_cpus: usize) {
    assert!(
        nr_cpus > 0,
        "per-CPU initialization requires at least one CPU"
    );
    let size = percpu_stride().checked_mul(nr_cpus).unwrap();
    let order = FrameOrder::from_size(size);

    let frame_manager = frame_manager();
    let frame = frame_manager
        .allocate(ZoneType::LinearMem, order)
        .or_else(|| frame_manager.allocate(ZoneType::MEM32, order))
        .unwrap();

    let area = PercpuArea::new(frame, nr_cpus);

    unsafe {
        let delta = ArchCpuLocal::activate(area.index(0));
        PERCPU_DELTA.write(delta);

        PERCPU_AREA.get().write(MaybeUninit::new(area))
    }
}
