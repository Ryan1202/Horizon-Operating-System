use core::{mem::MaybeUninit, ptr::copy_nonoverlapping};

use crate::{
    arch::x86::kernel::{acpi::X86Topology, interrupt::apic::ApicId},
    kernel::{
        interrupt::irq::{Affinity, IrqError, IrqNumber},
        memory::percpu::{PerCpuDyn, PerCpuInit},
        topology::CpuId,
    },
    lib::rust::{bitset::BitSet, spinlock::Spinlock},
};

pub(in crate::arch::x86) const MAX_VECTOR_COUNT: usize = 256;
pub const ERROR_VECTOR: u8 = 0xfe;
// 专用同步 IPI；EOI 后恢复本地中断，让待处理的 ISA vector 有机会进入
pub const SYNC_VECTOR: u8 = 0x30;
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

/// 逻辑 CPU 用于软件选址，APIC ID 供子控制器编程硬件。
#[derive(Clone, Copy)]
pub struct VectorRoute {
    /// 最终决定被分发的 CPU，为 `None` 表示发给所有 CPU
    pub cpu: Option<CpuId>,
    /// 仅在 `cpu` 为 `Some` 时有效，表示目标 CPU 的 APIC ID
    pub apic_id: Option<ApicId>,
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
    irqs: [MaybeUninit<IrqNumber>; MAX_VECTOR_COUNT],
}

// 不含自引用，每个 CPU 由动态初始化器构造独立值。
unsafe impl PerCpuInit for VectorMap {}

const fn reserved(vector: usize) -> bool {
    // CPU 异常、旧 ISA IRQ、系统调用、LAPIC timer、error 和 spurious。
    vector <= SYNC_VECTOR as usize || vector == 0x80 || vector >= ERROR_VECTOR as usize
}

impl VectorManager {
    pub const fn get() -> &'static Self {
        &VECTOR_MANAGER
    }

    pub(crate) fn init(&self) -> Result<(), IrqError> {
        let mut guard = self.inner.lock_irqsave();
        let cpus = X86Topology::get().cpus();

        let maps = PerCpuDyn::try_new_with(|cpuid| {
            let mut map = VectorMap {
                apic_id: cpus[cpuid.get() as usize].id(),
                available: 0,
                map: BitSet::zeroed(MAX_VECTOR_COUNT),
                irqs: [MaybeUninit::uninit(); MAX_VECTOR_COUNT],
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

        map.apic_id = apic_id;

        unsafe { copy_nonoverlapping(&inner.global, &mut map.map, 1) };

        Ok(())
    }

    /// 分配和映射发布在同一把锁下完成，只对已就绪 CPU 进行占用均衡。
    pub(crate) fn allocate(
        &self,
        irq: IrqNumber,
        affinity: Affinity,
        scope: VectorScope,
    ) -> Result<VectorRoute, IrqError> {
        let mut guard = self.inner.lock_irqsave();

        let state = unsafe { guard.assume_init_mut() };
        let maps = &mut state.maps;

        // ISA 的 virq 0..15 在启动时永久保留，对应固定 vector 0x20..0x2f。
        // 这些 vector 始终不属于通用分配池；仅路由反向映射随激活/停用变化。
        if irq.get() < 16 {
            let vector = 0x20 + irq.get();
            let cpu = maps
                .iter()?
                .find_map(|(cpu_id, map)| {
                    (!matches!(affinity, Affinity::Cpu(target) if target != cpu_id)
                        && map.map.test(cpu_id.get() as usize))
                    .then_some(cpu_id)
                })
                .ok_or(IrqError::NotFound)?;

            let map = maps
                .get_remote_mut(cpu)
                .expect("selected ISA target disappeared");
            map.irqs[vector] = MaybeUninit::new(irq);

            return Ok(VectorRoute {
                cpu: Some(cpu),
                apic_id: Some(map.apic_id),
                vector: vector as u8,
            });
        }

        if scope == VectorScope::Global {
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
                map.irqs[vector] = MaybeUninit::new(irq);
            }

            Ok(VectorRoute {
                cpu: None,
                apic_id: None,
                vector: vector as u8,
            })
        } else {
            let (cpu, _) = maps
                .iter()?
                .map(|(cpu, v)| (cpu, v.available))
                .max_by(|(_, a), (_, b)| a.cmp(b))
                .ok_or(IrqError::OutOfIrq)?;

            let map = maps
                .get_remote_mut(cpu)
                .expect("failed to get vector map for CPU");

            let vector = map.map.find_first_zero().ok_or(IrqError::OutOfIrq)?;

            map.map.set(vector);
            map.available -= 1;
            map.irqs[vector] = MaybeUninit::new(irq);

            Ok(VectorRoute {
                cpu: Some(cpu),
                apic_id: Some(map.apic_id),
                vector: vector as u8,
            })
        }
    }

    pub(crate) fn free(&self, route: VectorRoute, irq: IrqNumber) {
        let mut guard = self.inner.lock_irqsave();
        let state = unsafe { guard.assume_init_mut() };
        let maps = &state.maps;

        let vector = route.vector as usize;
        if let Some(cpu) = route.cpu {
            let map = maps
                .get_remote_mut(cpu)
                .expect("failed to get vector map for CPU");

            if irq.get() < 16 {
                map.irqs[vector] = MaybeUninit::uninit();
                return;
            }
            map.map.clear(vector);
            map.available += 1;
            map.irqs[vector] = MaybeUninit::uninit();
        } else {
            state.global.clear(vector);

            let maps = &mut state.maps;
            for (_, map) in maps.iter_mut().expect("failed to iterate vector maps") {
                map.map.clear(vector);
                map.available += 1;
                map.irqs[vector] = MaybeUninit::uninit();
            }
        }
    }

    /// 入口传入当前逻辑 CPU 和原始 IDT vector，不接受 ISA IRQ 编号。
    pub fn lookup(&self, cpu: CpuId, vector: u8) -> Option<IrqNumber> {
        let guard = self.inner.lock_irqsave();
        let maps = &unsafe { guard.assume_init_ref() }.maps;

        let map = maps.get_remote(cpu)?;

        map.map
            .test(vector as usize)
            .then_some(unsafe { map.irqs[vector as usize].assume_init() })
    }

    /// LAPIC 身份在当前 CPU 的短临界区内取得；这里只读取固定的 CPU 注册表。
    pub(super) fn lookup_apic(&self, apic_id: ApicId, vector: u8) -> Option<IrqNumber> {
        let guard = self.inner.lock_irqsave();
        let maps = &unsafe { guard.assume_init_ref() }.maps;

        for (_, map) in maps.iter().ok()? {
            if map.apic_id == apic_id {
                return Some(unsafe { map.irqs[vector as usize].assume_init() });
            }
        }

        None
    }
}
