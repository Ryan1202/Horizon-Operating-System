//! descriptor 保留完整内容；仅占位内容允许在表锁下替换

use super::{
    Flow, IrqData, IrqError, IrqNumber, IrqSharing, action::IrqAction, placeholder::Placeholder,
};
use crate::lib::rust::spinlock::Spinlock;
use core::{any::Any, mem, ptr::NonNull, sync::atomic::AtomicBool};

#[derive(PartialEq, Eq)]
pub(super) enum Status {
    Inactive,
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

    /// 宿主测试只开放软件分发，不调用 domain 生命周期回调
    #[cfg(test)]
    pub(crate) fn activate_for_test(&self) {
        let mut state = self.state.lock_irqsave();
        assert!(self.is_configured());
        assert!(state.status == Status::Inactive && state.head.is_some());
        state.status = Status::Active;
    }

    /// 表锁下替换占位；返回旧数据，由调用方在解锁后析构
    pub(super) fn realloc(&mut self, data: IrqData, flow: Flow) -> IrqData {
        assert!(!self.is_configured(), "cannot replace configured IRQ");
        self.flow = flow;

        mem::replace(&mut self.data, data)
    }
}
