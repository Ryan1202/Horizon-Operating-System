use core::arch::x86_64::__cpuid;

use crate::{
    arch::{
        ArchInterrupt, PhysAddr,
        x86::{
            drivers::interrupt::apic::{IoApics, LocalXApic, LvtEntry},
            kernel::msr::{IA32_APIC_BASE, rdmsr, wrmsr},
        },
    },
    kernel::{
        interrupt::Interrupt,
        interrupt::irq::{HardwareIrq, IrqError, Polarity, TriggerMode},
        thread::PreemptGuard,
        topology::CpuHardwareId,
    },
};

mod domain;
pub use domain::LocalApicDomain;

const GLOBAL_ENABLE: u64 = 1 << 11;
const X2APIC_ENABLE: u64 = 1 << 10;

/// 架构层统一检测并选择当前 CPU 的 LAPIC 实现。
/// 当前只实现 xAPIC；不支持的硬件或已开启的 x2APIC 模式直接终止启动。
pub fn init_current() -> Result<(), IrqError> {
    let _interrupt = ArchInterrupt::save_and_disable();
    let _preempt = PreemptGuard::new();

    assert!(__cpuid(1).edx & (1 << 9) != 0, "CPU does not support local APIC");
    let base = unsafe { rdmsr(IA32_APIC_BASE) };
    if base & X2APIC_ENABLE != 0 {
        // x2APIC 使用 MSR 寄存器访问，不能落入 xAPIC 的 MMIO 初始化路径。
        panic!("x2APIC mode is not implemented");
    }

    // 支持 x2APIC 并不表示已经启用；当前仍选择 xAPIC 后端。
    let address_bits = if __cpuid(0x80000000).eax >= 0x80000008 {
        __cpuid(0x80000008).eax & 0xff
    } else {
        36
    };
    assert!((12..64).contains(&address_bits), "unsupported APIC address width");
    let address_mask = ((1u64 << address_bits) - 1) & !0xfff;
    let address = PhysAddr::new((base & address_mask) as usize);

    if base & GLOBAL_ENABLE == 0 {
        unsafe { wrmsr(IA32_APIC_BASE, base | GLOBAL_ENABLE) };
    }
    // SAFETY: 当前 CPU 已确认并启用 xAPIC 模式，地址取自其 APIC_BASE MSR。
    unsafe { LocalXApic::init_current(address) }
}

pub type Gsi = HardwareIrq<IoApics>;

pub struct IoApicArg {
    pub gsi: Gsi,
    pub trigger_mode: TriggerMode,
    pub polarity: Polarity,
}

#[derive(Clone, Copy)]
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

pub trait LocalApic {
    /// 获取 Local APIC ID
    fn id(&self) -> ApicId;
    /// 设置 LVT 条目
    fn set_lvt_entry(&self, entry: LvtEntry, vector: u8) -> Result<(), IrqError>;
    /// 发送 EOI 信号
    fn eoi(&self);
}

pub trait IoApic {
    /// 获取 I/O APIC ID
    fn id(&self) -> ApicId;
    /// 获取 I/O APIC 的全局系统中断基址
    fn gsi_base(&self) -> Gsi;
    /// 设置 I/O APIC 的重定向表条目
    fn set_redirection_entry(&self, index: u8, entry: u64);
}
