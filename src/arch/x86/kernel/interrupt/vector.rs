use crate::{
    arch::x86::kernel::interrupt::apic::ApicId,
    kernel::{
        interrupt::irq::{Affinity, IrqError, IrqNumber},
        memory::percpu::{PerCpuDyn, PerCpuInit},
        topology::CpuId,
    },
    lib::rust::{bitset::BitSet, spinlock::Spinlock},
};

pub(in crate::arch::x86) const MAX_VECTOR_COUNT: usize = 256;
pub const ERROR_VECTOR: u8 = 0xfe;
pub const SPURIOUS_VECTOR: u8 = 0xff;

pub(super) static VECTOR_MANAGER: VectorManager = VectorManager {
    inner: Spinlock::new(None),
};

/// 逻辑 CPU 用于软件选址，APIC ID 供子控制器编程硬件。
#[derive(Clone, Copy)]
pub struct VectorRoute {
    pub cpu: CpuId,
    pub apic_id: ApicId,
    pub vector: u8,
}

pub struct VectorManager {
    inner: Spinlock<Option<PerCpuDyn<VectorMap>>>,
}

struct VectorMap {
    apic_id: Option<ApicId>,
    available: usize,
    map: BitSet<[usize; MAX_VECTOR_COUNT / usize::BITS as usize]>,
    irqs: [Option<IrqNumber>; MAX_VECTOR_COUNT],
}

// 不含自引用，每个 CPU 由动态初始化器构造独立值。
unsafe impl PerCpuInit for VectorMap {}

const fn reserved(vector: usize) -> bool {
    // CPU 异常、旧 ISA IRQ、系统调用、LAPIC error 和 spurious。
    vector < 0x30 || vector == 0x80 || vector >= ERROR_VECTOR as usize
}

impl VectorManager {
    pub const fn get() -> &'static Self {
        &VECTOR_MANAGER
    }

    pub(crate) fn init(&self) -> Result<(), IrqError> {
        let mut guard = self.inner.lock_irqsave();
        if guard.is_some() {
            return Err(IrqError::Busy);
        }
        *guard = Some(PerCpuDyn::try_new_with(|_| {
            let mut map = VectorMap {
                apic_id: None,
                available: 0,
                map: BitSet::zeroed(MAX_VECTOR_COUNT),
                irqs: [None; MAX_VECTOR_COUNT],
            };
            for vector in 0..MAX_VECTOR_COUNT {
                if reserved(vector) {
                    map.map.set(vector);
                } else {
                    map.available += 1;
                }
            }
            map
        })?);
        Ok(())
    }

    /// 调用前须先完成该 CPU 的 LAPIC 和 IDT 初始化
    pub(crate) fn register_cpu(&self, cpu: CpuId, apic_id: ApicId) -> Result<(), IrqError> {
        if apic_id.get() > u8::MAX as u32 {
            return Err(IrqError::Unsupported);
        }

        let guard = self.inner.lock_irqsave();

        let maps = guard.as_ref().ok_or(IrqError::NotFound)?;
        maps.get_remote_ptr(cpu)?;

        for (other, map) in maps.iter()? {
            if other != cpu && map.apic_id.is_some_and(|id| id.get() == apic_id.get()) {
                return Err(IrqError::Busy);
            }
        }

        let map = maps
            .get_remote_mut(cpu)
            .expect("failed to get vector map when registering CPU");

        if map.apic_id.is_some() {
            return Err(IrqError::Busy);
        }

        map.apic_id = Some(apic_id);
        Ok(())
    }

    /// 分配和映射发布在同一把锁下完成，只对已就绪 CPU 进行占用均衡。
    pub(crate) fn allocate(
        &self,
        irq: IrqNumber,
        affinity: Affinity,
    ) -> Result<VectorRoute, IrqError> {
        let guard = self.inner.lock_irqsave();

        let maps = guard.as_ref().ok_or(IrqError::NotFound)?;
        let mut selected: Option<(CpuId, usize)> = None;
        let mut eligible = false;

        for (cpu, map) in maps.iter()? {
            if matches!(affinity, Affinity::Cpu(target) if target != cpu) || map.apic_id.is_none() {
                continue;
            }

            eligible = true;

            if map.available == 0 {
                continue;
            }

            if selected.is_none_or(|(best, available)| {
                map.available > available || (map.available == available && cpu.get() < best.get())
            }) {
                selected = Some((cpu, map.available));
            }
        }

        let (cpu, _) = selected.ok_or(if eligible {
            IrqError::OutOfIrq
        } else {
            IrqError::NotFound
        })?;

        let map = maps
            .get_remote_mut(cpu)
            .expect("failed to get vector map for CPU");
        let vector = map.map.find_first_zero().ok_or(IrqError::OutOfIrq)?;

        map.map.set(vector);
        map.available -= 1;
        map.irqs[vector] = Some(irq);

        Ok(VectorRoute {
            cpu,
            apic_id: map.apic_id.unwrap(),
            vector: vector as u8,
        })
    }

    pub(crate) fn free(&self, route: VectorRoute, irq: IrqNumber) {
        let guard = self.inner.lock_irqsave();
        let maps = guard.as_ref().expect("vector manager not initialized");

        let map = maps
            .get_remote_mut(route.cpu)
            .expect("failed to get vector map for CPU");
        let vector = route.vector as usize;

        assert!(!reserved(vector));
        assert_eq!(map.irqs[vector], Some(irq));

        map.irqs[vector] = None;
        map.map.clear(vector);
        map.available += 1;
    }

    /// 入口传入当前逻辑 CPU 和原始 IDT vector，不接受 ISA IRQ 编号。
    pub fn lookup(&self, cpu: CpuId, vector: u8) -> Option<IrqNumber> {
        let guard = self.inner.lock_irqsave();
        let maps = guard.as_ref()?;

        maps.get_remote(cpu)?.irqs[vector as usize]
    }
}
