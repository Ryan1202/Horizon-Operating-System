//! 硬件 IRQ 深度、softirq 分发与调度器交接。

use core::{
    ffi::c_int,
    marker::PhantomData,
    sync::atomic::{AtomicU8, Ordering},
};

use crate::{arch::ArchInterrupt, kernel::thread::scheduler::SCHEDULER};

mod softirq;

static BOOT_CPU_INTERRUPT: InterruptState = InterruptState::new();

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
}

impl<'a, T: Interrupt> Drop for InterruptGuard<'a, T> {
    fn drop(&mut self) {
        T::restore(&self.status);
    }
}

/// 当前 CPU 的中断状态。单核阶段暂时返回启动 CPU 的全局实例；
/// 引入 per-CPU 后只需要替换此访问入口。
fn current() -> &'static InterruptState {
    &BOOT_CPU_INTERRUPT
}

struct InterruptState {
    hardirq_depth: AtomicU8,
    softirq_depth: AtomicU8,
}

impl InterruptState {
    const fn new() -> Self {
        Self {
            hardirq_depth: AtomicU8::new(0),
            softirq_depth: AtomicU8::new(0),
        }
    }

    fn enter_hardirq(&self) -> u8 {
        self.hardirq_depth
            .try_update(Ordering::Relaxed, Ordering::Relaxed, |depth| {
                depth.checked_add(1)
            })
            .expect("hardirq depth overflow")
    }

    fn leave_hardirq(&self) {
        self.hardirq_depth
            .try_update(Ordering::Relaxed, Ordering::Relaxed, |depth| {
                depth.checked_sub(1)
            })
            .expect("unbalanced hardirq guard");
    }

    fn enter_softirq(&self) -> u8 {
        self.softirq_depth
            .try_update(Ordering::Relaxed, Ordering::Relaxed, |depth| {
                depth.checked_add(1)
            })
            .expect("softirq depth overflow")
    }

    fn leave_softirq(&self) {
        self.softirq_depth
            .try_update(Ordering::Relaxed, Ordering::Relaxed, |depth| {
                depth.checked_sub(1)
            })
            .expect("unbalanced softirq guard");
    }

    fn softirq_active(&self) -> bool {
        self.softirq_depth.load(Ordering::Relaxed) != 0
    }

    fn in_softirq(&self) -> bool {
        self.hardirq_depth.load(Ordering::Relaxed) == 0 && self.softirq_active()
    }

    fn in_thread(&self) -> bool {
        self.hardirq_depth.load(Ordering::Relaxed) == 0 && !self.softirq_active()
    }
}

#[must_use = "a hard IRQ must finish device dispatch and EOI"]
struct HardIrqGuard {
    state: &'static InterruptState,
    outermost: bool,
    interrupted_softirq: bool,
}

impl HardIrqGuard {
    fn new() -> Self {
        let state = current();
        let interrupted_softirq = state.softirq_active();
        let depth = state.enter_hardirq();

        Self {
            state,
            outermost: depth == 0,
            interrupted_softirq,
        }
    }

    fn into_softirq(self) -> Option<SoftIrqGuard> {
        let state = self.state;
        let run_softirq = self.outermost && !self.interrupted_softirq;
        drop(self);

        if !run_softirq {
            return None;
        }

        assert_eq!(
            state.enter_softirq(),
            0,
            "softirq guard created while softirq is active"
        );
        Some(SoftIrqGuard { state })
    }
}

impl Drop for HardIrqGuard {
    fn drop(&mut self) {
        self.state.leave_hardirq();
    }
}

#[must_use = "softirq must drain pending work and produce a preemption point"]
struct SoftIrqGuard {
    state: &'static InterruptState,
}

impl Drop for SoftIrqGuard {
    fn drop(&mut self) {
        self.state.leave_softirq();
    }
}

/// 证明 IRQ 和 softirq 处理已经返回线程上下文。
#[must_use]
pub struct PreemptPoint {
    _phantom: PhantomData<()>,
}

impl PreemptPoint {
    pub fn new() -> Option<Self> {
        current().in_thread().then_some(Self {
            _phantom: PhantomData,
        })
    }

    fn from_softirq(_softirq: SoftIrqGuard) -> Self {
        Self {
            _phantom: PhantomData,
        }
    }

    pub fn try_preempt(self) {
        SCHEDULER.try_preempt(self);
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
        run_softirq(softirq).try_preempt();
    }
}

pub fn in_thread() -> bool {
    current().in_thread()
}

pub fn in_softirq() -> bool {
    current().in_softirq()
}
