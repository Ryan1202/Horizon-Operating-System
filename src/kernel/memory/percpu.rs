use core::{
    alloc::Layout,
    cell::{Cell, RefCell, UnsafeCell},
    marker::PhantomData,
    sync::atomic::Ordering,
};

use crate::{
    arch::ArchCpuLocal,
    kernel::{
        memory::{
            MemoryError,
            percpu::{
                area::{PERCPU_AREA, PercpuArea},
                init::{NR_CPUS_MAX, PERCPU_DELTAS, percpu_is_ready},
            },
        },
        thread::scheduler::PreemptGuard,
        topology::CpuId,
    },
};

unsafe extern "C" {
    pub static __percpu_start: u8;
    pub static __percpu_end: u8;
}

mod area;
mod chunk;
mod guard;
mod init;

pub use {guard::CpuLocalGuard, init::PerCpuInit};

#[macro_export]
macro_rules! cpu_local {
    () => {};
    (
        $(#[$attr:meta])*
        $vis:vis static $name:ident: $ty:ty = $value:expr;
        $($rest:tt)*
    ) => {
        $(#[$attr])*
        #[used]
        #[unsafe(link_section = ".data..percpu")]
        $vis static $name: $crate::kernel::memory::percpu::PerCpu<$ty> = $crate::kernel::memory::percpu::PerCpu::new($value);
        $crate::cpu_local!($($rest)*);
    };
}

cpu_local!(
    pub static CPU_DELTA: usize = 0;
);

pub trait CpuLocal {
    /// 根据 CPU unit 起点计算 GS delta，不修改当前 CPU 的 GS 寄存器。
    fn delta_for(base: *mut u8) -> usize;

    /// 激活当前 CPU 的 per-CPU 实例
    ///
    /// # Safety
    ///
    /// `base` 必须是一个始终有效的 per-CPU 实例的起始地址，并且该实例已经被初始化
    unsafe fn activate(base: *mut u8) -> usize;

    /// 获取 per-CPU 实例的指针
    fn get_ptr<T: PerCpuInit>(percpu: &PerCpu<T>) -> *const T {
        let delta = CPU_DELTA.read();
        unsafe { Self::get_ptr_for(percpu, delta) }
    }

    /// 获取某个 CPU 的 per-CPU 实例的指针
    ///
    /// # Safety
    ///
    /// `delta` 必须是一个有效的偏移量，指向某个 CPU 的 per-CPU 实例
    unsafe fn get_ptr_for<T: PerCpuInit>(percpu: &PerCpu<T>, delta: usize) -> *const T;

    fn get_ptr_dyn<T: PerCpuInit>(percpu: &PerCpuDyn<T>) -> *const T {
        let delta = CPU_DELTA.read();
        unsafe { Self::get_ptr_dyn_for(percpu, delta) }
    }

    unsafe fn get_ptr_dyn_for<T: PerCpuInit>(percpu: &PerCpuDyn<T>, delta: usize) -> *const T;
}

pub trait PerCpuReadWrite<T: PerCpuInit> {
    fn read(&self) -> T;
    fn write(&self, value: T);
}

pub trait PerCpuScalar<T: PerCpuInit>: PerCpuReadWrite<T> {
    fn add(&self, value: T);
    fn sub(&self, value: T);

    /// 对当前 CPU 的 per-CPU 实例进行加法操作，返回操作前的值
    fn fetch_add(&self, value: T) -> T;
    /// 对当前 CPU 的 per-CPU 实例进行减法操作，返回操作前的值
    fn fetch_sub(&self, value: T) -> T;

    fn increase(&self);
    fn decrease(&self);
}

#[repr(transparent)]
pub struct PerCpu<T: PerCpuInit> {
    value: T,
}

unsafe impl<T: PerCpuInit> Sync for PerCpu<T> {}

impl<T: PerCpuInit> PerCpu<T> {
    pub const fn new(value: T) -> Self {
        Self { value }
    }

    /// 获取 per-CPU 模板的指针，不可以直接访问该指针
    pub const fn get_template(&self) -> *const T {
        &raw const self.value
    }

    pub fn get_local<'a>(&self, preempt: &'a PreemptGuard) -> CpuLocalGuard<'a, T> {
        CpuLocalGuard::new(preempt, self)
    }

    pub fn get_remote(&self, cpu_id: CpuId) -> Result<*const T, MemoryError>
    where
        T: Sync,
    {
        let area = percpu_area()?;
        let delta = percpu_delta(area, cpu_id)?;
        // SAFETY: percpu_delta 已验证该 CPU 的 unit 已完成发布。
        Ok(unsafe { ArchCpuLocal::get_ptr_for(self, delta) })
    }
}

unsafe impl<T: PerCpuInit> PerCpuInit for Cell<T> {}
unsafe impl<T: PerCpuInit> PerCpuInit for UnsafeCell<T> {}
unsafe impl<T: PerCpuInit> PerCpuInit for RefCell<T> {}

pub(super) struct PerCpuDynHandle {
    handle: usize,
}

impl PerCpuDynHandle {
    pub fn new(dynamic_position: usize) -> Self {
        let handle = (&raw const __percpu_start).addr() + dynamic_position;
        Self { handle }
    }

    pub fn dynamic_position(&self) -> usize {
        self.handle - (&raw const __percpu_start).addr()
    }
}

pub struct PerCpuDyn<T: PerCpuInit> {
    handle: PerCpuDynHandle,
    _marker: PhantomData<T>,
}

impl<T: PerCpuInit> PerCpuDyn<T> {
    const fn new_in(handle: PerCpuDynHandle) -> Self {
        Self {
            handle,
            _marker: PhantomData,
        }
    }

    pub fn try_new_with(mut init: impl FnMut(CpuId) -> T) -> Result<Self, MemoryError> {
        let area = percpu_area()?;
        let layout = Layout::new::<T>();
        let handle = area.allocate(layout)?;
        let percpu = Self::new_in(handle);

        for cpu_id in 0..area.count() {
            let cpu_id = CpuId::new(cpu_id);
            let delta = percpu_delta(area, cpu_id)?;
            // SAFETY: handle 由 area 分配，delta 指向已发布的 CPU unit，且每个实例只在此处初始化一次。
            let ptr = unsafe { ArchCpuLocal::get_ptr_dyn_for(&percpu, delta) as *mut T };
            // SAFETY: 上述指针指向该 CPU 的独立、对齐且尚未初始化的 T 存储
            unsafe { ptr.write(init(cpu_id)) };
        }

        Ok(percpu)
    }

    pub const fn get_raw_handle(&self) -> usize {
        self.handle.handle
    }

    pub fn get_local<'a>(&'a self, preempt: &'a PreemptGuard) -> CpuLocalGuard<'a, T> {
        CpuLocalGuard::new_dyn(preempt, self)
    }

    pub fn get_remote(&self, cpu_id: CpuId) -> Result<*const T, MemoryError>
    where
        T: Sync,
    {
        let area = percpu_area()?;
        let delta = percpu_delta(area, cpu_id)?;
        // SAFETY: percpu_delta 已验证该 CPU 的 unit 已完成发布。
        Ok(unsafe { ArchCpuLocal::get_ptr_dyn_for(self, delta) })
    }
}

impl<T: PerCpuInit> Drop for PerCpuDyn<T> {
    fn drop(&mut self) {
        let area = percpu_area().expect("动态 per-CPU 对象析构时区域未初始化");

        for cpu_id in 0..area.count() {
            let cpu_id = CpuId::new(cpu_id);
            let delta = percpu_delta(area, cpu_id).expect("动态 per-CPU 对象析构时 CPU unit 无效");
            // SAFETY: handle 在当前析构前仍有效，且调用方必须保证不存在并发的远程访问。
            let ptr = unsafe { ArchCpuLocal::get_ptr_dyn_for(self, delta) as *mut T };
            unsafe {
                // SAFETY: 每个 CPU 实例恰好在构造时初始化一次，且此处恰好析构一次。
                ptr.drop_in_place();
            }
        }

        area.deallocate(&self.handle)
            .expect("动态 per-CPU 对象析构时 bitmap 状态损坏");
    }
}

pub(super) fn percpu_area() -> Result<&'static PercpuArea, MemoryError> {
    if !percpu_is_ready() {
        return Err(MemoryError::NotInitialized);
    }

    // SAFETY: Ready 仅在 PERCPU_AREA 完整写入且 BSP GS 已激活后以 Release 发布。
    Ok(unsafe { (*PERCPU_AREA.get()).assume_init_ref() })
}

pub(super) fn percpu_delta(area: &PercpuArea, cpu_id: CpuId) -> Result<usize, MemoryError> {
    if cpu_id.get() >= area.count() || cpu_id.get() >= NR_CPUS_MAX {
        return Err(MemoryError::ViolateConstraint);
    }

    let delta = PERCPU_DELTAS[cpu_id.get() as usize].load(Ordering::Acquire);
    if delta == 0 {
        return Err(MemoryError::ViolateConstraint);
    }

    Ok(delta)
}

pub fn percpu_template_size() -> usize {
    let (start, end) = (&raw const __percpu_start, &raw const __percpu_end);
    end.addr()
        .checked_sub(start.addr())
        .filter(|size| *size != 0)
        .expect("invalid per-CPU template bounds")
}
