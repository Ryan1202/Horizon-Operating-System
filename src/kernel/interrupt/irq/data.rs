use super::{Domain, HardwareIrq, IrqChip, IrqNumber, RawIrq};
use crate::kernel::memory::{MemoryError, kmalloc::Kmalloc};
use alloc::{boxed::Box, sync::Arc};
use core::any::Any;

/// 发布后拓扑不可变。free 在本层字段及 parent 析构之前运行。
pub struct IrqData {
    hwirq: RawIrq,
    domain: &'static dyn Domain,
    chip: Arc<dyn IrqChip, Kmalloc>,
    chip_data: Box<dyn Any + Send + Sync, Kmalloc>,

    parent: Option<Box<IrqData, Kmalloc>>,
}

impl IrqData {
    /// Domain 在 allocate 中构造本层；chip 与 chip_data 必须配对。
    pub const fn new<D: Domain + 'static>(
        hwirq: HardwareIrq<D>,
        domain: &'static dyn Domain,
        chip: Arc<dyn IrqChip, Kmalloc>,
        chip_data: Box<dyn Any + Send + Sync, Kmalloc>,
        parent: Option<Box<IrqData, Kmalloc>>,
    ) -> Self {
        Self {
            hwirq: hwirq.into_raw(),
            domain,
            chip,
            chip_data,
            parent,
        }
    }

    pub const fn raw_irq(&self) -> RawIrq {
        self.hwirq
    }

    pub fn domain(&self) -> &dyn Domain {
        self.domain
    }

    pub fn chip(&self) -> &dyn IrqChip {
        self.chip.as_ref()
    }

    pub fn parent(&self) -> Option<&IrqData> {
        self.parent.as_deref()
    }

    pub fn chip_data<T: Any + Send + Sync>(&self) -> &T {
        self.chip_data
            .downcast_ref()
            .expect("IRQ chip_data type mismatch")
    }

    pub fn alloc_parent(
        &mut self,
        irq: IrqNumber,
        domain: &'static dyn Domain,
        arg: &dyn Any,
    ) -> Result<&mut IrqData, super::IrqError> {
        let current = {
            let mut current = self;
            while current.parent.is_some() {
                current = unsafe { current.parent.as_deref_mut().unwrap_unchecked() };
            }
            current
        };

        let parent = Box::try_new_in(domain.allocate(irq, arg)?, Kmalloc::default())
            .map_err(|_| MemoryError::OutOfMemory)?;

        current.parent = Some(parent);

        Ok(current.parent.as_deref_mut().unwrap())
    }
}
