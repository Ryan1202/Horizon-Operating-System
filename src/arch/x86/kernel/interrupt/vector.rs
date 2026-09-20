use core::mem::MaybeUninit;

use crate::{
    arch::x86::kernel::interrupt::apic::ApicId,
    kernel::{
        interrupt::irq::{INVALID_IRQ, IrqError, IrqNumber},
        memory::percpu::{PerCpuDyn, PerCpuInit},
        topology::{CpuId, CpuMask, CpuRegistry},
    },
    lib::rust::{bitset::BitSet, spinlock::Spinlock},
};

pub(in crate::arch::x86) const MAX_VECTOR_COUNT: usize = 256;
pub const FIRST_DEVICE_VECTOR: u8 = 0x30;
pub const ERROR_VECTOR: u8 = 0xfe;
pub const SPURIOUS_VECTOR: u8 = 0xff;

#[derive(Clone, Copy, Default, PartialEq, Eq)]
pub enum VectorScope {
    #[default]
    PerCpu,
    /// IOAPIC Level EOI 按 vector 匹配，不能与其它 CPU 上的路由重号
    Global,
}

pub(super) static VECTOR_MANAGER: VectorManager = VectorManager {
    inner: Spinlock::new(MaybeUninit::uninit()),
};

/// 逻辑 CPU 用于软件选址，APIC ID 供子控制器编程硬件
#[derive(Clone, Copy)]
pub struct VectorRoute {
    /// 最终接收该 IRQ 的逻辑 CPU
    pub cpu: CpuId,
    /// 与 cpu 对应、供控制器编程物理目标字段的 APIC ID
    pub apic_id: ApicId,
    pub vector: u8,
}

pub struct VectorManager {
    inner: Spinlock<MaybeUninit<VectorInner>>,
}

struct VectorInner {
    /// 每个 CPU 一份的管理结构
    maps: PerCpuDyn<VectorMap>,
    /// 已全局分配的向量
    global: BitSet<[usize; MAX_VECTOR_COUNT / usize::BITS as usize]>,
}

struct VectorMap {
    /// 当前 CPU 的 APIC ID
    apic_id: ApicId,
    /// 剩余可用的中断向量号个数
    available: usize,
    /// 每个中断向量号的分配情况
    map: BitSet<[usize; MAX_VECTOR_COUNT / usize::BITS as usize]>,
    /// 每个中断向量号对应的全局 IRQ 编号，在 `map` 对应的位设置了之后才有效
    irqs: [IrqNumber; MAX_VECTOR_COUNT],
}

// 不含自引用，每个 CPU 由动态初始化器构造独立值
unsafe impl PerCpuInit for VectorMap {}

const fn reserved(vector: usize) -> bool {
    // CPU 异常、旧 ISA IRQ、系统调用、LAPIC timer、error 和 spurious
    vector < FIRST_DEVICE_VECTOR as usize || vector == 0x80 || vector >= ERROR_VECTOR as usize
}

impl VectorInner {
    fn alloc_cpu(&self, affinity: &CpuMask) -> Result<CpuId, IrqError> {
        let mut cpu = None;
        let mut acc = 0;
        for (cpu_id, map) in self.maps.iter()? {
            if map.available == 0 {
                continue;
            }

            if affinity.contains(cpu_id) && map.available > acc {
                acc = map.available;
                cpu = Some(cpu_id);
            }
        }
        cpu.ok_or(IrqError::NotFound)
    }
}

impl VectorManager {
    pub const fn get() -> &'static Self {
        &VECTOR_MANAGER
    }

    pub(crate) fn init(&self) -> Result<(), IrqError> {
        let mut guard = self.inner.lock_irqsave();
        let cpus = CpuRegistry::get();

        let maps = PerCpuDyn::try_new_with(|cpuid| {
            let mut map = VectorMap {
                // per-CPU 容量可以大于固件实际提供的 CPU 数，未注册槽位不可投递。
                apic_id: cpus
                    .hardware_id(cpuid)
                    .map(ApicId::from)
                    .unwrap_or(ApicId::new(u32::MAX)),
                available: 0,
                map: BitSet::zeroed(MAX_VECTOR_COUNT),
                irqs: [INVALID_IRQ; MAX_VECTOR_COUNT],
            };
            for vector in 0..MAX_VECTOR_COUNT {
                if reserved(vector) {
                    map.map.set(vector);
                } else {
                    map.available += 1;
                }
            }
            map
        })?;

        guard.write(VectorInner {
            maps,
            global: BitSet::zeroed(MAX_VECTOR_COUNT),
        });

        let global = &mut unsafe { guard.assume_init_mut() }.global;
        for vector in 0..MAX_VECTOR_COUNT {
            if reserved(vector) {
                global.set(vector);
            }
        }

        Ok(())
    }

    /// 调用前须先完成该 CPU 的 LAPIC 和 IDT 初始化
    pub(crate) fn register_cpu(&self, cpu_id: CpuId, apic_id: ApicId) -> Result<(), IrqError> {
        let guard = self.inner.lock_irqsave();

        let inner = unsafe { guard.assume_init_ref() };
        let maps = &inner.maps;

        let map = maps
            .get_remote_mut(cpu_id)
            .expect("failed to get vector map when registering CPU");

        if apic_id.get() >= 255 || CpuRegistry::get().hardware_id(cpu_id) != Some(apic_id.into()) {
            return Err(IrqError::Unsupported);
        }
        // global/ISA 的占用、反向映射及 available 已在分配时同步到所有槽位，
        // 上线不能重置它们，否则会破坏分配器的计数和所有权。
        map.apic_id = apic_id;

        Ok(())
    }

    /// 分配和映射发布在同一把锁下完成，只对已就绪 CPU 进行占用均衡。
    pub(crate) fn allocate(
        &self,
        irq: IrqNumber,
        affinity: &CpuMask,
        scope: VectorScope,
    ) -> Result<VectorRoute, IrqError> {
        let mut guard = self.inner.lock_irqsave();

        let online = CpuRegistry::get().online_cpus();
        let mut affinity = affinity.clone();
        affinity.intersect(&online.into());

        let state = unsafe { guard.assume_init_mut() };
        let cpu = state.alloc_cpu(&affinity)?;
        let maps = &mut state.maps;

        // ISA 的 virq 0..15 在启动时永久保留，对应固定 vector 0x20..0x2f。
        // 这些 vector 始终不属于通用分配池，反向映射也永久保留
        if irq.get() < 16 {
            let vector = 0x20 + irq.get();

            let map = maps.get_remote_mut(cpu).ok_or(IrqError::NotFound)?;

            return Ok(VectorRoute {
                cpu,
                apic_id: map.apic_id,
                vector: vector as u8,
            });
        }

        if scope == VectorScope::Global {
            let apic_id = maps.get_remote(cpu).ok_or(IrqError::NotFound)?.apic_id;

            let mut selected: Option<usize> = None;
            'outer: for vector in 0..MAX_VECTOR_COUNT {
                if reserved(vector) || state.global.test(vector) {
                    continue;
                }

                for (_, map) in maps.iter()? {
                    if map.available == 0 {
                        return Err(IrqError::OutOfIrq);
                    }
                    if map.map.test(vector) {
                        continue 'outer;
                    }
                }

                selected = Some(vector);
                break;
            }

            let vector = selected.ok_or(IrqError::OutOfIrq)?;
            state.global.set(vector);

            for (_, map) in maps.iter_mut()? {
                map.map.set(vector);
                map.available -= 1;
                map.irqs[vector] = irq;
            }

            Ok(VectorRoute {
                cpu,
                apic_id,
                vector: vector as u8,
            })
        } else {
            let map = maps.get_remote_mut(cpu).ok_or(IrqError::NotFound)?;

            let vector = map.map.find_first_zero().ok_or(IrqError::OutOfIrq)?;

            map.map.set(vector);
            map.available -= 1;
            map.irqs[vector] = irq;

            Ok(VectorRoute {
                cpu,
                apic_id: map.apic_id,
                vector: vector as u8,
            })
        }
    }

    pub(crate) fn free(&self, route: VectorRoute) {
        let mut guard = self.inner.lock_irqsave();
        let state = unsafe { guard.assume_init_mut() };
        let maps = &state.maps;

        if route.vector < FIRST_DEVICE_VECTOR {
            return;
        }

        let vector = route.vector as usize;

        if state.global.test(vector) {
            state.global.clear(vector);

            let maps = &mut state.maps;
            for (_, map) in maps.iter_mut().expect("failed to iterate vector maps") {
                map.map.clear(vector);
                map.available += 1;
                map.irqs[vector] = INVALID_IRQ;
            }
        } else {
            let map = maps
                .get_remote_mut(route.cpu)
                .expect("failed to get vector map for CPU");

            map.irqs[vector] = INVALID_IRQ;

            map.map.clear(vector);
            map.available += 1;
        }
    }

    pub(super) fn reserve(&self, irq: IrqNumber, vector: u8) -> Result<(), IrqError> {
        let mut guard = self.inner.lock_irqsave();
        let state = unsafe { guard.assume_init_mut() };
        let maps = &mut state.maps;

        if vector < 0x20 || vector >= FIRST_DEVICE_VECTOR {
            return Err(IrqError::InvalidArgument);
        }

        for (_, map) in maps.iter_mut()? {
            map.map.set(vector as usize);
            map.irqs[vector as usize] = irq;
        }

        Ok(())
    }

    /// 入口传入当前逻辑 CPU 和原始 IDT vector，不接受 ISA IRQ 编号。
    pub fn lookup(&self, cpu: CpuId, vector: u8) -> Option<IrqNumber> {
        let guard = self.inner.lock_irqsave();
        let maps = &unsafe { guard.assume_init_ref() }.maps;

        let map = maps.get_remote(cpu)?;

        let irq = map.irqs[vector as usize];
        (irq != INVALID_IRQ).then_some(irq)
    }

    /// LAPIC 身份在当前 CPU 的短临界区内取得；这里只读取固定的 CPU 注册表。
    pub(super) fn lookup_apic(&self, apic_id: ApicId, vector: u8) -> Option<IrqNumber> {
        let guard = self.inner.lock_irqsave();
        let maps = &unsafe { guard.assume_init_ref() }.maps;

        for (_, map) in maps.iter().ok()? {
            if map.apic_id == apic_id {
                let irq = map.irqs[vector as usize];
                return (irq != INVALID_IRQ).then_some(irq);
            }
        }

        None
    }
}
