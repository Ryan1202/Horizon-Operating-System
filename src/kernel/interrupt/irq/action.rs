//! Exclusive action 注册；本阶段只安装 handler，不激活硬件

use super::{
    Flow, IRQ_DESCRIPTORS, IrqError, IrqHandle, IrqNumber, descriptor::Status,
    sync::assert_management,
};
use crate::kernel::memory::kmalloc::Kmalloc;
use alloc::{boxed::Box, sync::Arc};
use core::ptr::NonNull;

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
}

/// 本阶段注册后仍保持 Inactive，尚不支持硬件投递或共享 action
pub fn request_irq(
    irq: IrqNumber,
    sharing: IrqSharing,
    handler: Arc<dyn IrqHandler, Kmalloc>,
) -> Result<IrqHandle, IrqError> {
    assert_management();

    if sharing != IrqSharing::Exclusive {
        return Err(IrqError::Unsupported);
    }

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

    let action = Box::<_, Kmalloc>::new_in(
        IrqAction {
            next: None,
            handler,
        },
        Kmalloc::default(),
    );

    let mut state = descriptor.state.lock_irqsave();
    if state.status != Status::Inactive || state.head.is_some() {
        // 解锁后才析构 action；handler 的析构允许重新进入注册 API
        drop(state);
        return Err(IrqError::Busy);
    }

    let action = Box::into_non_null_with_allocator(action).0;

    state.head = Some(action);
    state.sharing = sharing;

    drop(state);

    Ok(IrqHandle::new(descriptor_ptr, action))
}
