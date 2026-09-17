use core::{
    mem::ManuallyDrop,
    num::NonZero,
    sync::atomic::{AtomicPtr, Ordering},
};

use crate::{
    arch::{
        PhysAddr,
        x86::kernel::msr::{
            IA32_X2APIC_APIC_ID, IA32_X2APIC_EOI, IA32_X2APIC_ESR, IA32_X2APIC_LVT_CMCI,
            IA32_X2APIC_LVT_ERROR, IA32_X2APIC_LVT_LINT0, IA32_X2APIC_LVT_LINT1,
            IA32_X2APIC_LVT_PMI, IA32_X2APIC_LVT_THERMAL, IA32_X2APIC_LVT_TIMER, IA32_X2APIC_SIVR,
            IA32_X2APIC_TPR, IA32_X2APIC_VERSION, rdmsr, wrmsr,
        },
    },
    kernel::{
        interrupt::irq::IrqError,
        memory::{
            PageCacheType,
            page::{Pages, options::PageAllocOptions},
        },
    },
};

pub(super) enum CommonReg {
    Id,
    Version,
    Tpr,
    Eoi,
    Svr,
    Esr,
    // LVTs
    Timer,
    Cmci,
    Lint0,
    Lint1,
    Error,
    PerfCounter,
    Thermal,
}

use CommonReg::*;

pub(super) trait Regs {
    fn read_common(&self, reg: CommonReg) -> u32;
    fn write_common(&self, reg: CommonReg, value: u32);
}

mod offsets {
    pub const ID: usize = 0x20;
    pub const VERSION: usize = 0x30;
    pub const TPR: usize = 0x80;
    pub const EOI: usize = 0xb0;
    pub const SVR: usize = 0xf0;
    pub const ESR: usize = 0x280;
    pub const LVT_CMCI: usize = 0x2f0;
    pub const LVT_TIMER: usize = 0x320;
    pub const LVT_THERMAL: usize = 0x330;
    pub const LVT_PERF: usize = 0x340;
    pub const LVT_LINT0: usize = 0x350;
    pub const LVT_LINT1: usize = 0x360;
    pub const LVT_ERROR: usize = 0x370;
}

/// LVT 源与 IDT vector 是两种编号，不能用 vector 索引寄存器
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum LvtEntry {
    Timer,
    Thermal,
    Performance,
    Lint0,
    Lint1,
    Error,
    Cmci,
}

impl LvtEntry {
    pub(super) const fn get_reg(self) -> CommonReg {
        match self {
            Self::Timer => Timer,
            Self::Thermal => Thermal,
            Self::Performance => PerfCounter,
            Self::Lint0 => Lint0,
            Self::Lint1 => Lint1,
            Self::Error => Error,
            Self::Cmci => Cmci,
        }
    }

    pub(super) const fn supported(self, max_lvt: u32) -> bool {
        match self {
            Self::Timer | Self::Lint0 | Self::Lint1 => true,
            Self::Error => max_lvt >= 3,
            Self::Performance => max_lvt >= 4,
            Self::Thermal => max_lvt >= 5,
            Self::Cmci => max_lvt >= 6,
        }
    }
}

pub(super) struct MmioRegs {
    base: AtomicPtr<u32>,
}

impl Clone for MmioRegs {
    fn clone(&self) -> Self {
        Self {
            base: AtomicPtr::new(self.base.load(Ordering::Relaxed)),
        }
    }
}

impl Regs for MmioRegs {
    fn read_common(&self, reg: CommonReg) -> u32 {
        let offset = Self::reg_offset(reg);
        self.read(offset)
    }

    fn write_common(&self, reg: CommonReg, value: u32) {
        let offset = Self::reg_offset(reg);
        self.write(offset, value);
    }
}

impl MmioRegs {
    const fn reg_offset(reg: CommonReg) -> usize {
        match reg {
            Id => offsets::ID,
            Version => offsets::VERSION,
            Tpr => offsets::TPR,
            Eoi => offsets::EOI,
            Svr => offsets::SVR,
            Esr => offsets::ESR,
            Timer => offsets::LVT_TIMER,
            Thermal => offsets::LVT_THERMAL,
            PerfCounter => offsets::LVT_PERF,
            Lint0 => offsets::LVT_LINT0,
            Lint1 => offsets::LVT_LINT1,
            Error => offsets::LVT_ERROR,
            Cmci => offsets::LVT_CMCI,
        }
    }

    pub(super) fn new_bsp(address: PhysAddr) -> Result<Self, IrqError> {
        let frame = address.to_frame_number();
        let offset = address.page_offset();

        let page = PageAllocOptions::mmio(
            frame,
            const { NonZero::new(1).unwrap() },
            PageCacheType::Uncached,
        )
        .allocate()?;
        let page = ManuallyDrop::new(page);

        // 移动 Pages 或扩容 Vec 不改变页映射的虚拟地址。
        let base = (page.start_addr() + offset).as_mut_ptr();
        let base = AtomicPtr::new(base);

        Ok(Self { base })
    }

    fn read(&self, offset: usize) -> u32 {
        // SAFETY: 偏移仅来自本模块的 LAPIC 寄存器，位于全局持有的映射内
        unsafe {
            self.base
                .load(Ordering::Relaxed)
                .byte_add(offset)
                .read_volatile()
        }
    }

    fn write(&self, offset: usize, value: u32) {
        unsafe {
            self.base
                .load(Ordering::Relaxed)
                .byte_add(offset)
                .write_volatile(value)
        };
        // 读回同一设备，保证寄存器写入完成后再进行下一步操作
        let _ = self.read(offsets::ID);
    }
}

#[derive(Clone)]
pub(super) struct MsrRegs;

impl MsrRegs {
    const fn reg_address(reg: CommonReg) -> u32 {
        match reg {
            Id => IA32_X2APIC_APIC_ID,
            Version => IA32_X2APIC_VERSION,
            Tpr => IA32_X2APIC_TPR,
            Eoi => IA32_X2APIC_EOI,
            Svr => IA32_X2APIC_SIVR,
            Esr => IA32_X2APIC_ESR,
            Timer => IA32_X2APIC_LVT_TIMER,
            Cmci => IA32_X2APIC_LVT_CMCI,
            Lint0 => IA32_X2APIC_LVT_LINT0,
            Lint1 => IA32_X2APIC_LVT_LINT1,
            Error => IA32_X2APIC_LVT_ERROR,
            PerfCounter => IA32_X2APIC_LVT_PMI,
            Thermal => IA32_X2APIC_LVT_THERMAL,
        }
    }
}

impl Regs for MsrRegs {
    fn read_common(&self, reg: CommonReg) -> u32 {
        let addr = Self::reg_address(reg);
        unsafe { rdmsr(addr) as u32 }
    }

    fn write_common(&self, reg: CommonReg, value: u32) {
        let addr = Self::reg_address(reg);
        unsafe { wrmsr(addr, value as u64) };
    }
}
