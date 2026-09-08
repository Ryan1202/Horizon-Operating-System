//! 一个 IrqHandle 精确拥有一个 action，Drop 同步注销该 action。
//!
//! lease 保证注销期间 descriptor 不会被释放；action 摘链后再释放 handler。

use super::{IrqNumber, action::IrqAction, descriptor::Status, sync::assert_management};
use crate::kernel::{
    interrupt::irq::{IRQ_DESCRIPTORS, IrqError, IrqSharing, descriptor::IrqDescriptor, domain},
    memory::kmalloc::Kmalloc,
};
use alloc::boxed::Box;
use core::ptr::NonNull;

#[must_use = "dropping the IRQ handle synchronously unregisters its handler"]
pub struct IrqHandle {
    irq: IrqNumber,
    descriptor: &'static Option<NonNull<IrqDescriptor>>,
    pub(super) action: Option<NonNull<IrqAction>>,
}

// 节点身份只由管理路径访问；handler 已要求 Send + Sync。
unsafe impl Send for IrqHandle {}
unsafe impl Sync for IrqHandle {}

impl IrqHandle {
    pub fn new(
        irq: IrqNumber,
        sharing: IrqSharing,
        action: NonNull<IrqAction>,
    ) -> Result<Self, IrqError> {
        let descriptor = IRQ_DESCRIPTORS.lease(irq, sharing)?;
        Ok(Self {
            irq,
            descriptor,
            action: Some(action),
        })
    }

    pub fn irq(&self) -> IrqNumber {
        self.irq
    }
}

impl Drop for IrqHandle {
    fn drop(&mut self) {
        assert_management(); // 同时排除 handler 注销自己或同一 IRQ 的另一个 action。

        let action = self.action.take().expect("IRQ action already removed");
        let Some(descriptor) = self.descriptor else {
            return;
        };
        let descriptor = unsafe { descriptor.as_ref() };
        // let _management = descriptor.management_lock.lock();

        {
            let mut state = descriptor.state.lock_irqsave();

            state.delete_action(action);

            if state.head.is_none() {
                state.status = Status::Stopping;
                domain::deactivate(&descriptor.data);
                state.status = Status::Inactive;
            }
        }

        // // handler 私有对象的 Drop 也可能注销其他 handle，不能在管理锁内析构。
        // drop(_management);

        unsafe {
            drop(Box::<_, Kmalloc>::from_raw_in(
                action.as_ptr(),
                Kmalloc::default(),
            ));
        }
    }
}
