//! descriptor 保留完整内容；仅占位内容允许在表锁下替换

use super::{
    Flow, IrqData, IrqError, IrqNumber, IrqSharing, action::IrqAction, placeholder::Placeholder,
};
use crate::{
    kernel::interrupt::irq::{domain, flow},
    lib::rust::spinlock::{SpinIrqGuard, Spinlock},
};
use core::{
    any::Any,
    hint::spin_loop,
    mem,
    ptr::NonNull,
    sync::atomic::{AtomicBool, Ordering},
};

#[derive(PartialEq, Eq)]
pub(super) enum Status {
    Inactive,
    Disabled,
    Active,
    Stopping,
}

pub(super) struct State {
    pub(super) status: Status,
    pub(super) head: Option<NonNull<IrqAction>>,
    pub(super) sharing: IrqSharing,
    pub(super) pending: bool,
}

impl State {
    pub(super) fn freeze(&mut self, descriptor: &IrqDescriptor) {
        let active = self.status == Status::Active;
        self.status = Status::Stopping;

        if active && descriptor.flow != Flow::Simple {
            descriptor.data.chip().mask(&descriptor.data);
        }
    }

    /// 检查在共享 IRQ 下是否仍有启用的节点
    pub(super) fn any_enabled(&self) -> bool {
        let mut action = self.head;

        while let Some(pointer) = action {
            // SAFETY: 调用方持有状态锁；链表修改也持锁且已排空分发
            let current = unsafe { pointer.as_ref() };
            if current.enabled {
                return true;
            }

            action = current.next;
        }

        false
    }

    const fn new(sharing: IrqSharing) -> Self {
        Self {
            status: Status::Inactive,
            head: None,
            sharing,
            pending: false,
        }
    }
}

pub struct IrqDescriptor {
    pub irq: IrqNumber,
    pub data: IrqData,
    pub flow: Flow,
    pub(super) state: Spinlock<State>,
    // 仅在 state 锁内修改；注销可在锁外自旋观察，再持锁检查
    pub(super) in_progress: AtomicBool,
}

impl IrqDescriptor {
    pub(super) fn lock_stable(&self) -> SpinIrqGuard<'_, &mut State> {
        loop {
            let state = self.state.lock_irqsave();
            if state.status != Status::Stopping
                && !(state.status == Status::Inactive && state.head.is_some())
            {
                return state;
            }
            drop(state);
            spin_loop();
        }
    }

    /// 等待所有执行者完成
    pub(super) fn drain(&self) {
        loop {
            while self.in_progress.load(Ordering::Relaxed) {
                spin_loop();
            }

            let _state = self.state.lock_irqsave();

            if !self.in_progress.load(Ordering::Relaxed) {
                return;
            }
        }
    }

    /// 调用方拥有 Stopping 更新阶段，且已排空旧执行者
    pub(super) fn finish_update(&self) {
        let mut state = self.state.lock_irqsave();

        if state.any_enabled() {
            state.status = Status::Active;
            let pending = state.pending;

            if self.flow != Flow::Simple {
                self.data.chip().unmask(&self.data);
            }

            drop(state);

            // 硬件事件也可先取得执行权并接管 pending；软件入口不会伪造 ack/EOI。
            if pending {
                flow::replay(self);
            }
        } else {
            drop(state);
            domain::synchronize(&self.data);

            let mut state = self.state.lock_irqsave();
            state.pending = false;
            state.status = Status::Disabled;
        }
    }

    pub fn empty(irq: IrqNumber) -> Self {
        Self {
            irq,
            data: Placeholder::data(irq),
            flow: Flow::Bad,
            state: Spinlock::new(State::new(IrqSharing::Exclusive)),
            in_progress: AtomicBool::new(false),
        }
    }

    pub fn new(
        irq: IrqNumber,
        sharing: IrqSharing,
        domain: &'static dyn super::Domain,
        flow: Flow,
        arg: &dyn Any,
    ) -> Result<Self, IrqError> {
        let data = domain.allocate(irq, arg)?;

        Ok(Self {
            irq,
            data,
            flow,
            state: Spinlock::new(State::new(sharing)),
            in_progress: AtomicBool::new(false),
        })
    }

    pub fn is_configured(&self) -> bool {
        self.data.chip().downcast_ref::<Placeholder>().is_none()
    }

    /// 表锁下替换占位；返回旧数据，由调用方在解锁后析构
    pub(super) fn realloc(&mut self, data: IrqData, flow: Flow) -> IrqData {
        self.flow = flow;

        mem::replace(&mut self.data, data)
    }
}
