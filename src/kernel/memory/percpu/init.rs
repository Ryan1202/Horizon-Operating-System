use core::sync::atomic::{
    AtomicBool, AtomicI8, AtomicI16, AtomicI32, AtomicI64, AtomicIsize, AtomicU8, AtomicU16,
    AtomicU32, AtomicU64, AtomicUsize, Ordering,
};

use crate::{
    CACHELINE_SIZE,
    arch::ArchCpuLocal,
    kernel::memory::{
        MemoryError,
        frame::{
            FrameAllocator, buddy::FrameOrder, frame_manager, reference::UniqueFrames,
            zone::ZoneType,
        },
        percpu::{
            CPU_DELTA, CpuLocal, PercpuArea, area::PERCPU_AREA, chunk::DYNAMIC_TARGET_SIZE,
            percpu_template_size,
        },
    },
};

/// 允许的最大 CPU 数量
pub const NR_CPUS_MAX: usize = 256;
pub static PERCPU_DELTAS: [AtomicUsize; NR_CPUS_MAX] = [const { AtomicUsize::new(0) }; NR_CPUS_MAX];

const PERCPU_UNINITIALIZED: u8 = 0;
const PERCPU_INITIALIZING: u8 = 1;
const PERCPU_READY: u8 = 2;

static PERCPU_STATE: AtomicU8 = AtomicU8::new(PERCPU_UNINITIALIZED);

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

pub(crate) fn percpu_is_ready() -> bool {
    PERCPU_STATE.load(Ordering::Acquire) == PERCPU_READY
}

fn allocate_backing(nr_cpus: usize, dynamic_start: usize) -> Result<UniqueFrames, MemoryError> {
    let target_unit_size = (dynamic_start + DYNAMIC_TARGET_SIZE).next_power_of_two();
    let target_size = target_unit_size * nr_cpus;
    let target_order = FrameOrder::from_size(target_size);

    let frame_manager = frame_manager();
    frame_manager
        .allocate(ZoneType::LinearMem, target_order)
        .or_else(|| frame_manager.allocate(ZoneType::MEM32, target_order))
        .ok_or(MemoryError::OutOfMemory)
}

pub(crate) fn try_percpu_init(nr_cpus: usize) -> Result<(), MemoryError> {
    if nr_cpus == 0 || nr_cpus > NR_CPUS_MAX {
        return Err(MemoryError::ViolateConstraint);
    }

    let frame = allocate_backing(
        nr_cpus,
        percpu_template_size().next_multiple_of(CACHELINE_SIZE),
    )?;
    let area = PercpuArea::try_new(frame, nr_cpus)?;

    PERCPU_STATE.store(PERCPU_INITIALIZING, Ordering::Relaxed);

    for cpu_id in 0..nr_cpus {
        let delta = ArchCpuLocal::delta_for(area.index(cpu_id));

        // SAFETY: delta 由该 CPU unit 的有效起点计算，目标是模板复制后的 CPU_DELTA 实例
        let cpu_delta = unsafe { ArchCpuLocal::get_ptr_for(&CPU_DELTA, delta) as *mut usize };
        // SAFETY: 每个 CPU unit 的 CPU_DELTA 仅在启动阶段写入一次
        unsafe { cpu_delta.write(delta) };

        PERCPU_DELTAS[cpu_id].store(delta, Ordering::Relaxed);
    }

    unsafe {
        // SAFETY: area 在 Ready 发布前一次性写入，之后只通过不可变引用访问。
        PERCPU_AREA.get().write(core::mem::MaybeUninit::new(area));
        // SAFETY: CPU0 unit 已复制模板并写入 CPU_DELTA，可以安全作为 BSP 的 GS 基准。
        ArchCpuLocal::activate((*PERCPU_AREA.get()).assume_init_ref().index(0));
    }

    PERCPU_STATE.store(PERCPU_READY, Ordering::Relaxed);
    Ok(())
}

#[unsafe(no_mangle)]
pub extern "C" fn percpu_init(nr_cpus: usize) {
    try_percpu_init(nr_cpus).expect("per-CPU 初始化失败")
}
