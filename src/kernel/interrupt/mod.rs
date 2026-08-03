//! 硬件 IRQ 深度、softirq 分发与调度器交接。

use core::{ffi::c_int, marker::PhantomData};

use crate::{
    arch::ArchInterrupt,
    cpu_local,
    kernel::{
        memory::percpu::{PerCpuReadWrite, PerCpuScalar},
        thread::scheduler::PreemptGuard,
    },
};

mod softirq;

cpu_local!(
    static HARDIRQ_DEPTH: u8 = 0;
    static SOFTIRQ_DEPTH: u8 = 0;
);

unsafe extern "C" {
    fn device_irq_handler(irq: c_int);
    fn interrupt_eoi(irq: c_int);
}

pub trait Interrupt: Sized {
    type Status;

    /// 获取当前中断状态。
    fn get_status() -> Self::Status;
    /// 启用中断
    fn enable();
    /// 禁用中断
    fn disable();

    /// 开启中断并等待下一次中断。
    fn wait();

    /// 禁用中断并保存当前中断状态，返回调用前的中断状态。
    fn save_and_disable<'a>() -> InterruptGuard<'a, Self>;
    /// 恢复中断状态
    fn restore(status: &Self::Status);
}

pub struct InterruptGuard<'a, T: Interrupt> {
    status: T::Status,
    _phantom: PhantomData<&'a ()>,
}

impl<'a, T: Interrupt> InterruptGuard<'a, T> {
    pub const fn new(status: T::Status) -> Self {
        Self {
            status,
            _phantom: PhantomData,
        }
    }

    pub const fn status(&self) -> &T::Status {
        &self.status
    }
}

impl<'a, T: Interrupt> Drop for InterruptGuard<'a, T> {
    fn drop(&mut self) {
        T::restore(&self.status);
    }
}

fn enter_hardirq() -> u8 {
    assert!(HARDIRQ_DEPTH.read() < u8::MAX, "hardirq depth overflow");
    HARDIRQ_DEPTH.fetch_add(1)
}

fn leave_hardirq() {
    assert!(HARDIRQ_DEPTH.read() > 0, "unbalanced hardirq guard");
    HARDIRQ_DEPTH.decrease();
}

fn enter_softirq() -> u8 {
    assert!(SOFTIRQ_DEPTH.read() < u8::MAX, "softirq depth overflow");
    SOFTIRQ_DEPTH.fetch_add(1)
}

fn leave_softirq() {
    assert!(SOFTIRQ_DEPTH.read() > 0, "unbalanced softirq guard");
    SOFTIRQ_DEPTH.decrease();
}

fn hardirq_active() -> bool {
    HARDIRQ_DEPTH.read() != 0
}

fn softirq_active() -> bool {
    SOFTIRQ_DEPTH.read() != 0
}

fn in_softirq() -> bool {
    !hardirq_active() && softirq_active()
}

pub fn in_thread() -> bool {
    !hardirq_active() && !softirq_active()
}

#[must_use = "a hard IRQ must finish device dispatch and EOI"]
struct HardIrqGuard {
    outermost: bool,
    interrupted_softirq: bool,
}

impl HardIrqGuard {
    fn new() -> Self {
        let interrupted_softirq = softirq_active();
        let depth = enter_hardirq();

        Self {
            outermost: depth == 0,
            interrupted_softirq,
        }
    }

    fn into_softirq(self) -> Option<SoftIrqGuard> {
        let run_softirq = self.outermost && !self.interrupted_softirq;
        drop(self);

        if !run_softirq {
            return None;
        }

        let depth = enter_softirq();
        assert_eq!(depth, 0, "softirq guard created while softirq is active");
        Some(SoftIrqGuard {
            _phantom: PhantomData,
        })
    }
}

impl Drop for HardIrqGuard {
    fn drop(&mut self) {
        leave_hardirq();
    }
}

#[must_use = "softirq must drain pending work and produce a preemption point"]
struct SoftIrqGuard {
    _phantom: PhantomData<()>,
}

impl Drop for SoftIrqGuard {
    fn drop(&mut self) {
        leave_softirq();
    }
}

/// 证明 IRQ 和 softirq 处理已经返回线程上下文。
#[must_use]
pub struct PreemptPoint {
    _phantom: PhantomData<()>,
}

impl PreemptPoint {
    pub fn new() -> Option<Self> {
        in_thread().then_some(Self {
            _phantom: PhantomData,
        })
    }

    fn from_softirq(_softirq: SoftIrqGuard) -> Self {
        Self {
            _phantom: PhantomData,
        }
    }

    pub fn try_preempt(self, guard: PreemptGuard) {
        guard.try_preempt(self);
    }
}

fn run_softirq(guard: SoftIrqGuard) -> PreemptPoint {
    ArchInterrupt::enable();

    loop {
        softirq::drain();

        // 关闭中断后复查 pending，封闭最后一次空检查与 guard 释放之间的竞态。
        ArchInterrupt::disable();
        if softirq::pending() {
            ArchInterrupt::enable();
            continue;
        }

        let point = PreemptPoint::from_softirq(guard);
        ArchInterrupt::disable();
        return point;
    }
}

/// 仅供架构 IRQ 入口在校验 IRQ 编号后调用。
pub fn handle(irq: u8) {
    let hardirq = HardIrqGuard::new();

    // SAFETY: 架构入口已经校验 IRQ 编号；设备分发和控制器 EOI
    // 由现有的 C 中断框架提供。
    unsafe {
        device_irq_handler(irq as c_int);
        interrupt_eoi(irq as c_int);
    }

    if let Some(softirq) = hardirq.into_softirq() {
        run_softirq(softirq).try_preempt(PreemptGuard::new());
    }
}
