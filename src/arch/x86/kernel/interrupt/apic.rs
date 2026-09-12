use core::{arch::x86_64::__cpuid, cell::OnceCell};

use crate::{
    arch::{
        ArchInterrupt, PhysAddr,
        x86::{
            drivers::interrupt::apic::{IoApics, LocalApic},
            kernel::{
                acpi::BOOT_CAPABILITIES,
                interrupt::vector::VectorManager,
                msr::{IA32_APIC_BASE, rdmsr, wrmsr},
            },
        },
    },
    cpu_local,
    kernel::{
        interrupt::{
            Interrupt,
            irq::{Flow, HardwareIrq, IrqError, Polarity, TriggerMode},
        },
        memory::percpu::PerCpuInit,
        thread::PreemptGuard,
        topology::{CpuHardwareId, CpuId},
    },
};

mod domain;
pub use domain::LocalApicDomain;

const GLOBAL_ENABLE: u64 = 1 << 11;
const X2APIC_ENABLE: u64 = 1 << 10;
pub(crate) const DEFAULT_LAPIC_ADDRESS: usize = 0xFEE00000;

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum EoiMode {
    Broadcast,
    Directed,
}

struct LocalEoiMode(OnceCell<EoiMode>);

// 模板为空，策略在本 CPU 可接收中断之前只发布一次
unsafe impl PerCpuInit for LocalEoiMode {}

cpu_local! {
    static EOI_MODE: LocalEoiMode = LocalEoiMode(OnceCell::new());
}

pub fn current_eoi_mode(preempt: &PreemptGuard) -> EoiMode {
    *EOI_MODE
        .get_local(preempt)
        .0
        .get()
        .expect("EOI mode not initialized")
}

/// 集合初始化后固定本 CPU 的 EOI 策略，再将 CPU 加入路由候选
///
/// # Safety
///
/// cpu 必须是当前逻辑 CPU，IDT 必须已安装可分配向量及 error/spurious 入口
///
/// 旧 C APIC 驱动不能再操作此 CPU 的 LAPIC
pub unsafe fn enable_current(cpu: CpuId) -> Result<(), IrqError> {
    let _interrupt = ArchInterrupt::save_and_disable();
    let preempt = PreemptGuard::new();

    let all_support_eoi = IoApics::get().all_support_eoi().ok_or(IrqError::NotFound)?;
    let local = EOI_MODE.get_local(&preempt);
    let lapic = LocalApic::get();

    let directed = all_support_eoi && lapic.supports_eoi_suppression();

    // SAFETY: 当前 CPU 尚未发布为路由候选，且能力检测已完成
    unsafe { lapic.set_eoi_broadcast_suppressed(directed) };

    local.0.get_or_init(|| {
        if directed {
            EoiMode::Directed
        } else {
            EoiMode::Broadcast
        }
    });

    lapic.software_enable();

    VectorManager::get().register_cpu(cpu, lapic.id())
}

/// 架构层统一检测并选择当前 CPU 的 LAPIC 实现
pub fn init_bsp() -> Result<(), IrqError> {
    let _interrupt = ArchInterrupt::save_and_disable();
    let _preempt = PreemptGuard::new();

    let cpuid_1 = __cpuid(1);
    assert!(
        cpuid_1.edx & (1 << 9) != 0,
        "CPU does not support local APIC"
    );

    if cpuid_1.ecx & (1 << 21) != 0 {
        // x2APIC 可能出现 I/O APIC 不支持的 APIC ID >= 255 的情况，导致无法注册到路由表中，
        // 所以要启用 x2APIC 模式必须最大 APIC ID < 255 或者已经支持并启用了 VT-d 的 Interrupt Remapping
        x2apic_init_bsp()
    } else {
        xapic_init_bsp()
    }
}

fn x2apic_init_bsp() -> Result<(), IrqError> {
    let base = unsafe { rdmsr(IA32_APIC_BASE) };

    // BIOS 的默认行为是在交给 OS 前，如果所有 APIC ID 都小于 255 就启用 xAPIC 模式，否则启用 x2APIC 模式。
    // 所以如果没有处于 x2APIC 模式，那就还是走 xAPIC 初始化路径
    if base & X2APIC_ENABLE == 0 {
        return xapic_init_bsp();
    }

    unimplemented!("x2APIC mode is not implemented");
}

fn xapic_init_bsp() -> Result<(), IrqError> {
    let base = unsafe { rdmsr(IA32_APIC_BASE) };

    // 获取 xAPIC 地址宽度，默认 36 位
    let address_bits = if __cpuid(0x80000000).eax >= 0x80000008 {
        __cpuid(0x80000008).eax & 0xff
    } else {
        36
    };

    assert!(
        (12..64).contains(&address_bits),
        "unsupported APIC address width"
    );

    let address_mask = ((1u64 << address_bits) - 1) & !0xfff;
    let address = base & address_mask;

    let expected_addr = unsafe { (*BOOT_CAPABILITIES.get()).lapic_address } as u64;
    assert!(
        address == expected_addr,
        "APIC address mismatch: MSR provided address {:#x} != Default address from ACPI / Standard {:#x}",
        address,
        expected_addr
    );

    let address = PhysAddr::new(address as usize);

    if base & GLOBAL_ENABLE == 0 {
        unsafe { wrmsr(IA32_APIC_BASE, base | GLOBAL_ENABLE) };
    }

    LocalApic::init_xapic_bsp(address)
}

pub type Gsi = HardwareIrq<IoApics>;

pub struct IoApicArg {
    pub gsi: Gsi,
    pub trigger_mode: TriggerMode,
    pub polarity: Polarity,
}

impl IoApicArg {
    pub const fn flow(&self) -> Flow {
        match self.trigger_mode {
            TriggerMode::Edge => Flow::Edge,
            TriggerMode::Level => Flow::FastEoi,
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(transparent)]
pub struct ApicId(u32);

impl ApicId {
    pub const fn new(id: u32) -> Self {
        Self(id)
    }

    pub const fn get(&self) -> u32 {
        self.0
    }
}

impl From<CpuHardwareId> for ApicId {
    fn from(hardware_id: CpuHardwareId) -> Self {
        Self::new(hardware_id.get_raw())
    }
}

impl Into<CpuHardwareId> for ApicId {
    fn into(self) -> CpuHardwareId {
        CpuHardwareId::new(self.get())
    }
}

pub struct IoApicInfo {
    pub id: ApicId,
    pub address: usize,
    pub gsi_base: Gsi,
}

impl Default for IoApicInfo {
    fn default() -> Self {
        Self {
            id: ApicId::new(0),
            address: 0xFEC00000,
            gsi_base: Gsi::new(0),
        }
    }
}

pub trait IoApic {
    /// 获取 I/O APIC ID
    fn id(&self) -> ApicId;
    /// 获取 I/O APIC 的全局系统中断基址
    fn gsi_base(&self) -> Gsi;
    /// 设置 I/O APIC 的重定向表条目
    fn set_redirection_entry(&self, index: u8, entry: u64);
}
