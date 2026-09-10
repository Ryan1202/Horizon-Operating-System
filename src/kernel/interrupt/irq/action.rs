//! action 注册；首个节点激活 domain，后续 Shared 节点复用路由

use super::{
    Flow, IRQ_DESCRIPTORS, IrqError, IrqHandle, IrqNumber, assert_management, descriptor::Status,
};
use crate::kernel::{
    interrupt::irq::{Affinity, domain},
    memory::kmalloc::Kmalloc,
};
use alloc::{boxed::Box, sync::Arc};
use core::{hint::spin_loop, ptr::NonNull};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum IrqSharing {
    Exclusive,
    Shared,
}

pub trait IrqHandler: Send + Sync {
    fn handle(&self, irq: IrqNumber) -> Option<()>;
}

impl<F: Fn(IrqNumber) -> Option<()> + Send + Sync> IrqHandler for F {
    fn handle(&self, irq: IrqNumber) -> Option<()> {
        self(irq)
    }
}

pub struct IrqAction {
    pub(super) next: Option<NonNull<IrqAction>>,
    pub(super) handler: Arc<dyn IrqHandler, Kmalloc>,
    pub(super) enabled: bool,
}

/// 成功后由 handle 显式 enable_irq
///
/// 激活期间以 head 占用注册位置
pub fn request_irq(
    irq: IrqNumber,
    sharing: IrqSharing,
    handler: Arc<dyn IrqHandler, Kmalloc>,
) -> Result<IrqHandle, IrqError> {
    assert_management();

    let descriptor = IRQ_DESCRIPTORS.lookup(irq).ok_or(IrqError::NotFound)?;
    if !descriptor.is_configured() {
        return Err(IrqError::InvalidArgument);
    }

    let flow = descriptor.flow;

    if !matches!(
        flow,
        Flow::Edge | Flow::Level | Flow::FastEoi | Flow::Simple
    ) {
        return Err(IrqError::Unsupported);
    }

    let descriptor_ptr = NonNull::from(&*descriptor);
    drop(descriptor);

    // SAFETY: 表锁下已确认配置完成，之后不再重配；本阶段 descriptor 常驻
    let descriptor = unsafe { descriptor_ptr.as_ref() };

    let mut action = Box::<_, Kmalloc>::new_in(
        IrqAction {
            next: None,
            handler,
            enabled: false,
        },
        Kmalloc::default(),
    );

    let mut state = loop {
        let state = descriptor.state.lock_irqsave();
        if state.head.is_some()
            && !(sharing == IrqSharing::Shared && state.sharing == IrqSharing::Shared)
        {
            drop(state);
            return Err(IrqError::Busy);
        }

        if state.status != Status::Stopping
            && !(state.status == Status::Inactive && state.head.is_some())
        {
            break state;
        }
        drop(state);
        spin_loop();
    };

    if state.head.is_some() {
        state.freeze(descriptor);
        drop(state);
        descriptor.drain();

        let mut state = descriptor.state.lock_irqsave();

        action.next = state.head;
        let action = Box::into_non_null_with_allocator(action).0;
        state.head = Some(action);

        drop(state);
        descriptor.finish_update();

        return Ok(IrqHandle::new(descriptor_ptr, action));
    }

    let action = Box::into_non_null_with_allocator(action).0;

    state.head = Some(action);
    state.sharing = sharing;

    drop(state);

    let mut affinity = Affinity::Auto;
    if let Err(error) = domain::activate(irq, &descriptor.data, &mut affinity) {
        descriptor.state.lock_irqsave().head = None;

        // SAFETY: 尚未开放分发，激活失败已回滚；节点在锁外归还和析构
        unsafe {
            drop(Box::<_, Kmalloc>::from_raw_in(
                action.as_ptr(),
                Kmalloc::default(),
            ))
        };
        return Err(error);
    }

    descriptor.state.lock_irqsave().status = Status::Disabled;

    Ok(IrqHandle::new(descriptor_ptr, action))
}
