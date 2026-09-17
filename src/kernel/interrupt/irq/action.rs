//! action 注册；首个节点激活 domain，后续 Shared 节点复用路由

use super::IrqNumber;
use crate::kernel::memory::kmalloc::Kmalloc;
use alloc::sync::Arc;
use core::{ptr::NonNull, sync::atomic::AtomicBool};

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
    pub(super) enabled: AtomicBool,
}
