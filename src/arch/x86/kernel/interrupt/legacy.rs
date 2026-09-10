//! ISA 路由与占位配置；通用 request_irq 不处理 ISA 特例

use crate::{
    arch::x86::{
        drivers::interrupt::apic::IoApics,
        kernel::interrupt::apic::{Gsi, IoApicArg},
    },
    kernel::{
        interrupt::irq::{
            self, IRQ_DESCRIPTORS, IrqDescriptor, IrqError, IrqHandle, IrqHandler, IrqNumber,
            IrqReservation, IrqSharing, Polarity, TriggerMode,
        },
        memory::kmalloc::Kmalloc,
    },
    lib::rust::spinlock::Spinlock,
};
use alloc::sync::Arc;

const IRQ_COUNT: usize = 16;

static ISA_NUMBERS: Spinlock<Option<IrqReservation>> = Spinlock::new(None);
static LEGACY_IRQ_ROUTES: LegacyIrq = LegacyIrq::new();

#[derive(Clone, Copy)]
pub struct IrqOverride {
    gsi: Gsi,
    active_low: bool,
    level_triggered: bool,
}

impl IrqOverride {
    pub const fn new(gsi: Gsi, active_low: bool, level_triggered: bool) -> Self {
        Self {
            gsi,
            active_low,
            level_triggered,
        }
    }

    fn argument(self) -> IoApicArg {
        IoApicArg {
            gsi: self.gsi,
            trigger_mode: if self.level_triggered {
                TriggerMode::Level
            } else {
                TriggerMode::Edge
            },
            polarity: if self.active_low {
                Polarity::Low
            } else {
                Polarity::High
            },
        }
    }
}

/// 每项独立记录 ACPI 描述的路由；空项表示该 ISA IRQ 不可用
pub struct LegacyIrq {
    map: Spinlock<[Option<IrqOverride>; IRQ_COUNT]>,
}

impl LegacyIrq {
    pub const fn new() -> Self {
        Self {
            map: Spinlock::new([None; IRQ_COUNT]),
        }
    }

    pub const fn get() -> &'static Self {
        &LEGACY_IRQ_ROUTES
    }

    /// ACPI 解析过程中逐项填写，不为未描述的 IRQ 补路由
    pub fn override_irq(&self, irq: usize, route: IrqOverride) {
        self.map.lock_irqsave()[irq] = Some(route);
    }

    pub fn route(&self, isa_irq: u8) -> Option<IoApicArg> {
        self.map.lock_irqsave()[isa_irq as usize].map(IrqOverride::argument)
    }
}

/// 启动阶段发布具有占位内容的 descriptor，不分配 IOAPIC/LAPIC mapping
pub(super) fn init_irqs() -> Result<(), IrqError> {
    let mut irqs = IrqReservation::reserve((0..IRQ_COUNT).into())?;

    for irq in irqs.iter() {
        irqs.publish(IrqDescriptor::empty(irq))?;
    }

    *ISA_NUMBERS.lock_irqsave() = Some(irqs);

    Ok(())
}

pub(crate) fn isa_irq(irq: u8) -> Option<IrqNumber> {
    ISA_NUMBERS.lock_irqsave().as_ref()?.get(irq as usize)
}

/// 平台必须先完成 ISA 编号、ACPI 路由和 IOAPIC 初始化。
/// 配置成功后常驻；首个 action 激活 domain，各 handle 显式开放自己的 handler。
pub(crate) fn request_isa_irq(
    isa_irq_number: u8,
    sharing: IrqSharing,
    handler: Arc<dyn IrqHandler, Kmalloc>,
) -> Result<IrqHandle, IrqError> {
    irq::assert_management();

    if isa_irq_number as usize >= IRQ_COUNT {
        return Err(IrqError::InvalidArgument);
    }

    let irq = isa_irq(isa_irq_number).ok_or(IrqError::NotFound)?;
    let arg = LegacyIrq::get()
        .route(isa_irq_number)
        .ok_or(IrqError::NotFound)?;
    let configured = IRQ_DESCRIPTORS
        .lookup(irq)
        .ok_or(IrqError::NotFound)?
        .is_configured();

    if !configured {
        match IRQ_DESCRIPTORS.realloc(irq, IoApics::get(), arg.flow(), &arg) {
            Ok(()) => {}
            Err(IrqError::Busy)
                if IRQ_DESCRIPTORS
                    .lookup(irq)
                    .is_some_and(|descriptor| descriptor.is_configured()) => {}
            Err(error) => return Err(error),
        }
    }

    irq::request_irq(irq, sharing, handler)
}
