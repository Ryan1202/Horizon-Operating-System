use core::{any::Any, hint::spin_loop};

use alloc::{boxed::Box, sync::Arc};

use crate::{
    arch::x86::{
        drivers::interrupt::apic::LocalApic,
        kernel::interrupt::vector::{VectorManager, VectorRoute, VectorScope},
    },
    kernel::{
        interrupt::irq::{Domain, HardwareIrq, IrqChip, IrqData, IrqError, IrqNumber, RawIrq},
        memory::{MemoryError, kmalloc::Kmalloc},
        topology::CpuMask,
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
    scope: VectorScope,
    route: Spinlock<Route>,
}

struct Route {
    current: Option<VectorRoute>,
    old: Option<VectorRoute>,
}

impl LocalApicDomain {
    pub const fn get() -> &'static Self {
        &LOCAL_APIC_DOMAIN
    }

    pub(crate) fn route(data: &IrqData) -> Option<VectorRoute> {
        let local = data.chip_data::<LocalApicData>();
        let route = local.route.lock_irqsave();
        route.current.clone()
    }
}

impl Domain for LocalApicDomain {
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
                scope,
                route: Spinlock::new(Route {
                    current: None,
                    old: None,
                }),
            },
            Kmalloc::default(),
        );

        let chip = Arc::new_in(LocalApicChip, Kmalloc::default());

        Ok(IrqData::new(
            // 本层源编号在整个 descriptor 生命周期内稳定；不等同于 IDT vector。
            HardwareIrq::<Self>::new(RawIrq::new(irq.get() as u32)),
            Self::get(),
            chip,
            local,
            None,
        ))
    }

    unsafe fn activate(
        &self,
        irq: IrqNumber,
        data: &IrqData,
        affinity: &CpuMask,
    ) -> Result<CpuMask, IrqError> {
        let local = data.chip_data::<LocalApicData>();

        let mut effective = CpuMask::new_zeroed().ok_or(MemoryError::OutOfMemory)?;
        let mut route = local.route.lock_irqsave();

        assert!(route.current.is_none());
        if route.old.is_some() {
            drop(route);
            let _ = self.try_reclaim_route(data);
            route = local.route.lock_irqsave();
        }

        let allocated = VectorManager::get().allocate(irq, affinity, local.scope)?;
        route.current = Some(allocated);

        effective.set(allocated.cpu);

        Ok(effective)
    }

    unsafe fn deactivate(&self, data: &IrqData) -> Result<(), IrqError> {
        let mut route = data.chip_data::<LocalApicData>().route.lock_irqsave();
        if route.old.is_some() {
            return Err(IrqError::Busy);
        }

        route.old = route.current.take();

        Ok(())
    }

    fn synchronize(&self, data: &IrqData) {
        let local = data.chip_data::<LocalApicData>();
        let vector = local
            .route
            .lock_irqsave()
            .current
            .map(|r| r.vector)
            .unwrap_or(0);
        let lapic = LocalApic::get();

        while lapic.is_busy(vector) {
            spin_loop();
        }
    }

    unsafe fn update_affinity(
        &self,
        irq: IrqNumber,
        data: &IrqData,
        affinity: &CpuMask,
    ) -> Result<CpuMask, IrqError> {
        let local = data.chip_data::<LocalApicData>();
        let mut effective = CpuMask::new_zeroed().ok_or(MemoryError::OutOfMemory)?;

        let mut route = local.route.lock_irqsave();

        if route.old.is_some() {
            return Err(IrqError::Busy);
        }

        let allocated = VectorManager::get().allocate(irq, affinity, local.scope)?;
        effective.set(allocated.cpu);

        // 旧的路由需要等待目标 CPU 完成所有已发起的事件后才能释放
        route.old = route.current.replace(allocated);

        Ok(effective)
    }

    fn try_reclaim_route(&self, data: &IrqData) -> Result<(), IrqError> {
        let local = data.chip_data::<LocalApicData>();

        let mut route = local.route.lock_irqsave();

        if let Some(old) = &route.old {
            let lapic = LocalApic::get();
            if old.apic_id != lapic.id() {
                return Err(IrqError::NotFound);
            }
            if lapic.is_busy(old.vector) {
                // 如果旧的向量还在忙，不能立即释放
                return Err(IrqError::Busy);
            }

            VectorManager::get().free(unsafe { route.old.take().unwrap_unchecked() });
        }

        Ok(())
    }
}

impl IrqChip for LocalApicChip {
    // LAPIC 无法逐个屏蔽外部 vector；子 chip 必须操作自己的中断源
    fn mask(&self, _data: &IrqData) {}
    fn unmask(&self, _data: &IrqData) {}
    fn ack(&self, _data: &IrqData) {}

    fn eoi(&self, _data: &IrqData) {
        // 只对当前处理硬件事件的 CPU 发送 EOI，不按路由远程访问 LAPIC
        LocalApic::get().eoi();
    }
}
