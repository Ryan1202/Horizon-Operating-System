use core::any::Any;

use alloc::{boxed::Box, sync::Arc};

use crate::{
    arch::x86::{
        drivers::interrupt::apic::LocalXApic,
        kernel::interrupt::{
            apic::LocalApic,
            synchronize_vector,
            vector::{VectorManager, VectorRoute, VectorScope},
        },
    },
    kernel::{
        interrupt::irq::{Affinity, Domain, HardwareIrq, IrqChip, IrqData, IrqError, IrqNumber},
        memory::kmalloc::Kmalloc,
    },
    lib::rust::spinlock::Spinlock,
};

static LOCAL_APIC_DOMAIN: LocalApicDomain = LocalApicDomain;

/// CPU/vector 根层
///
/// 不负责 IOAPIC pin 或 LVT 中断源的屏蔽
pub struct LocalApicDomain;

struct LocalApicChip;

struct LocalApicData {
    irq: IrqNumber,
    scope: VectorScope,
    route: Spinlock<Option<VectorRoute>>,
}

impl LocalApicDomain {
    pub const fn get() -> &'static Self {
        &LOCAL_APIC_DOMAIN
    }

    /// 传入本层 IrqData，子 domain 在父层 activate 完成后读取投递目标
    pub fn route(data: &IrqData) -> Option<VectorRoute> {
        *data.chip_data::<LocalApicData>().route.lock_irqsave()
    }

    fn release(data: &IrqData) {
        let local = data.chip_data::<LocalApicData>();
        let mut route = local.route.lock_irqsave();
        if let Some(current) = route.take() {
            VectorManager::get().free(current, local.irq);
        }
    }
}

impl Domain for LocalApicDomain {
    /// arg 为 VectorScope；兼容 () 表示 PerCpu。目标 CPU 由 activate 决定
    fn allocate(&self, irq: IrqNumber, arg: &dyn Any) -> Result<IrqData, IrqError> {
        let scope = if let Some(scope) = arg.downcast_ref::<VectorScope>() {
            *scope
        } else if arg.is::<()>() {
            VectorScope::PerCpu
        } else {
            return Err(IrqError::InvalidArgument);
        };

        let local = Box::new_in(
            LocalApicData {
                irq,
                scope,
                route: Spinlock::new(None),
            },
            Kmalloc::default(),
        );

        let chip = Arc::new_in(LocalApicChip, Kmalloc::default());

        Ok(IrqData::new(
            // 本层源编号在整个 descriptor 生命周期内稳定；不等同于 IDT vector。
            HardwareIrq::<Self>::new(irq.get() as u32),
            Self::get(),
            chip,
            local,
            None,
        ))
    }

    fn free(&self, data: &IrqData) {
        Self::release(data);
    }

    fn activate(
        &self,
        irq: IrqNumber,
        data: &IrqData,
        affinity: &mut Affinity,
    ) -> Result<(), IrqError> {
        let local = data.chip_data::<LocalApicData>();
        if local.irq != irq {
            return Err(IrqError::InvalidIrqNumber(irq.get()));
        }

        let mut route = local.route.lock_irqsave();
        if route.is_some() {
            return Err(IrqError::Busy);
        }

        let allocated = VectorManager::get().allocate(irq, *affinity, local.scope)?;

        *route = Some(allocated);
        *affinity = Affinity::Cpu(allocated.cpu);

        Ok(())
    }

    fn deactivate(&self, data: &IrqData) {
        // core 已排空硬件和软件执行，或此路由从未开放过投递
        Self::release(data);
    }

    fn synchronize(&self, data: &IrqData) {
        let route = Self::route(data).expect("synchronize without LAPIC route");
        synchronize_vector(route);
    }
}

impl IrqChip for LocalApicChip {
    // LAPIC 无法逐个屏蔽外部 vector；子 chip 必须操作自己的中断源
    fn mask(&self, _data: &IrqData) {}
    fn unmask(&self, _data: &IrqData) {}
    fn ack(&self, _data: &IrqData) {}

    fn eoi(&self, _data: &IrqData) {
        // 只对当前处理硬件事件的 CPU 发送 EOI，不按路由远程访问 LAPIC
        LocalXApic::with_current(|lapic| lapic.eoi())
            .expect("EOI before local APIC initialization");
    }
}
