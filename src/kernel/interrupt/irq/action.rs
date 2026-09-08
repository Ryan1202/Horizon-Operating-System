//! handler 接口、私有 action 节点以及注册事务
//!
//! 节点先完成初始化再头插发布；旧读者只看旧 head，因此注册无需停住读者

use core::ptr::NonNull;

use alloc::sync::Arc;

use crate::kernel::memory::kmalloc::Kmalloc;

use super::IrqNumber;

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

pub(super) struct IrqAction {
    pub(super) next: Option<NonNull<IrqAction>>,
    pub(super) handler: Arc<dyn IrqHandler, Kmalloc>,
}

// pub fn request_irq(
//     source: impl IrqSource,
//     sharing: IrqSharing,
//     handler: Arc<dyn IrqHandler, Kmalloc>,
// ) -> Result<IrqHandle, IrqError> {
//     request(source, sharing, handler, false)
// }

// pub fn request_percpu_irq(
//     source: impl IrqSource,
//     handler: Arc<dyn IrqHandler, Kmalloc>,
// ) -> Result<IrqHandle, IrqError> {
//     request(source, IrqSharing::Exclusive, handler, true)
// }

// fn request(
//     source: impl IrqSource,
//     sharing: IrqSharing,
//     handler: Arc<dyn IrqHandler, Kmalloc>,
//     percpu: bool,
// ) -> Result<IrqHandle, IrqError> {
//     assert_management();

//     let lease = source.resolve()?;
//     let descriptor = lease.descriptor();

//     let action = Box::<_, Kmalloc>::try_new_in(
//         IrqAction {
//             next: None,
//             handler,
//         },
//         Kmalloc::default(),
//     )
//     .map_err(|_| IrqError::OutOfMemory)?;

//     let action = {
//         // let _management = descriptor.management_lock.lock();

//         if (descriptor.flow == Flow::PerCpu) != percpu {
//             return Err(IrqError::InvalidMapping);
//         }

//         descriptor
//             .state
//             .lock_irqsave()
//             .add_action(action, sharing)?
//     };

//     Ok(IrqHandle {
//         lease,
//         action: Some(action),
//     })
// }
