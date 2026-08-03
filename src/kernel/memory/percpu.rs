use core::cell::{Cell, RefCell, UnsafeCell};

use crate::{
    arch::ArchPageTable,
    kernel::{
        memory::{arch::ArchMemory, percpu::area::PercpuArea},
        thread::scheduler::PreemptGuard,
    },
};

unsafe extern "C" {
    pub static __percpu_start: u8;
    pub static __percpu_end: u8;
}

mod area;
mod guard;
mod init;

pub(super) use init::percpu_init;
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
    pub static PERCPU_DELTA: usize = 0;
);

pub trait CpuLocal {
    /// 激活当前 CPU 的 per-CPU 实例
    ///
    /// # Safety
    ///
    /// `base` 必须是一个始终有效的 per-CPU 实例的起始地址，并且该实例已经被初始化
    unsafe fn activate(base: *mut u8) -> usize;

    /// 获取 per-CPU 实例的指针
    fn get_ptr<T: PerCpuInit>(percpu: &PerCpu<T>) -> *const T {
        let delta = PERCPU_DELTA.read();
        unsafe { Self::get_ptr_for(percpu, delta) }
    }

    /// 获取某个 CPU 的 per-CPU 实例的指针
    ///
    /// # Safety
    ///
    /// `delta` 必须是一个有效的偏移量，指向某个 CPU 的 per-CPU 实例
    unsafe fn get_ptr_for<T: PerCpuInit>(percpu: &PerCpu<T>, delta: usize) -> *const T;
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
}

unsafe impl<T: PerCpuInit> PerCpuInit for Cell<T> {}
unsafe impl<T: PerCpuInit> PerCpuInit for UnsafeCell<T> {}
unsafe impl<T: PerCpuInit> PerCpuInit for RefCell<T> {}

pub fn percpu_template_size() -> usize {
    let (start, end) = (&raw const __percpu_start, &raw const __percpu_end);
    end.addr()
        .checked_sub(start.addr())
        .filter(|size| *size != 0)
        .expect("invalid per-CPU template bounds")
}

pub fn percpu_stride() -> usize {
    percpu_template_size()
        .checked_next_multiple_of(ArchPageTable::PAGE_SIZE)
        .expect("per-CPU stride overflow")
}
