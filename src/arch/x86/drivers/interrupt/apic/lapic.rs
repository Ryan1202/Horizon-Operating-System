use core::{cell::SyncUnsafeCell, mem::MaybeUninit};

use crate::{
    arch::{
        ArchInterrupt, PhysAddr,
        x86::{
            drivers::interrupt::apic::lapic::reg::{MmioRegs, MsrRegs, Regs},
            kernel::interrupt::{
                apic::ApicId,
                vector::{ERROR_VECTOR, FIRST_DEVICE_VECTOR, SPURIOUS_VECTOR},
            },
        },
    },
    kernel::{
        interrupt::{Interrupt, irq::IrqError},
        thread::PreemptGuard,
    },
};

mod reg;

use reg::CommonReg::*;
pub(crate) use reg::LvtEntry;

const SOFTWARE_ENABLE: u32 = 1 << 8;
const SUPPRESS_EOI_BROADCAST: u32 = 1 << 12;
const SUPPRESS_EOI_BROADCASE_SUPPORT: u32 = 1 << 24;
const LVT_MASK: u32 = 1 << 16;

static LAPIC: SyncUnsafeCell<MaybeUninit<LocalApic>> = SyncUnsafeCell::new(MaybeUninit::uninit());

pub struct LocalApic {
    max_lvt_entry: u32,
    lapic_type: LapicType,
}

#[derive(Clone)]
enum LapicType {
    XApic(MmioRegs),
    X2Apic(MsrRegs),
}

impl LocalApic {
    pub fn get<'a>() -> &'a Self {
        unsafe { (*LAPIC.get()).assume_init_ref() }
    }

    const fn reg(&self) -> &dyn Regs {
        match &self.lapic_type {
            LapicType::XApic(mmio) => mmio,
            LapicType::X2Apic(msr) => msr,
        }
    }

    pub(crate) fn init_xapic_bsp(address: PhysAddr) -> Result<(), IrqError> {
        let lapic = Self::init(LapicType::XApic(MmioRegs::new_bsp(address)?))?;

        unsafe { LAPIC.get().write(MaybeUninit::new(lapic)) };

        Ok(())
    }

    pub(crate) fn init_ap() {
        let apic = unsafe { (*LAPIC.get()).assume_init_ref() };
        let lapic_type = apic.lapic_type.clone();

        // 每个 lapic 使用相同的配置，但是都需要单独初始化一遍
        let _ = Self::init(lapic_type);
    }

    /// 初始化当前 CPU，保持软件禁用和所有 LVT 屏蔽
    fn init(lapic_type: LapicType) -> Result<Self, IrqError> {
        let _interrupt = ArchInterrupt::save_and_disable();
        let _preempt = PreemptGuard::new();

        let reg: &dyn Regs = match &lapic_type {
            LapicType::XApic(mmio) => mmio,
            LapicType::X2Apic(msr) => msr,
        };

        let svr = reg.read_common(Svr);
        reg.write_common(Svr, svr & !SOFTWARE_ENABLE);

        let max_lvt_entry = (reg.read_common(Version) >> 16) & 0xff;

        // Error 先配置合法向量，其余源清除固件遗留的 delivery mode
        for entry in [
            LvtEntry::Error,
            LvtEntry::Timer,
            LvtEntry::Lint0,
            LvtEntry::Lint1,
            LvtEntry::Performance,
            LvtEntry::Thermal,
            LvtEntry::Cmci,
        ] {
            if entry.supported(max_lvt_entry) {
                reg.write_common(entry.get_reg(), LVT_MASK | ERROR_VECTOR as u32);
            }
        }

        if LvtEntry::Error.supported(max_lvt_entry) {
            reg.write_common(Esr, 0);
            let _ = reg.read_common(Esr);
        }

        reg.write_common(Tpr, 0);

        // 使用广播 EOI，保留其它位，关闭软件启用和 EOI broadcast suppression
        let mut svr = svr & !(0xff | SOFTWARE_ENABLE | SUPPRESS_EOI_BROADCAST);
        svr |= SPURIOUS_VECTOR as u32;
        reg.write_common(Svr, svr);

        Ok(Self {
            max_lvt_entry,
            lapic_type,
        })
    }

    pub(crate) fn software_enable(&self) {
        let reg = self.reg();
        reg.write_common(Svr, reg.read_common(Svr) | SOFTWARE_ENABLE);
    }

    pub fn supports_eoi_suppression(&self) -> bool {
        self.reg().read_common(Version) & SUPPRESS_EOI_BROADCASE_SUPPORT != 0
    }

    /// # Safety
    ///
    /// 架构层须在 CPU 开始接收中断前调用。启用抑制时已确认全部 IOAPIC 支持显式 EOI，
    /// 且当前 LAPIC 支持该位。不能在活动路由存在时切换
    pub(crate) unsafe fn set_eoi_broadcast_suppressed(&self, suppressed: bool) {
        let reg = self.reg();
        let svr = reg.read_common(Svr);
        reg.write_common(
            Svr,
            if suppressed {
                svr | SUPPRESS_EOI_BROADCAST
            } else {
                svr & !SUPPRESS_EOI_BROADCAST
            },
        );
    }

    pub fn version(&self) -> u8 {
        (self.reg().read_common(Version) & 0xff) as u8
    }

    pub fn supports(&self, entry: LvtEntry) -> bool {
        entry.supported(self.max_lvt_entry)
    }

    pub fn mask(&self, entry: LvtEntry) -> Result<(), IrqError> {
        let reg = self.reg();
        self.check_entry(entry)?;
        reg.write_common(entry.get_reg(), reg.read_common(entry.get_reg()) | LVT_MASK);
        Ok(())
    }

    /// 对应 vector 必须已由调用方安装入口并绑定 handler
    pub fn unmask(&self, entry: LvtEntry) -> Result<(), IrqError> {
        self.check_entry(entry)?;
        let reg = self.reg();

        let value = reg.read_common(entry.get_reg());
        if entry != LvtEntry::Error && value as u8 == ERROR_VECTOR {
            // 初始化占位向量不能作为普通 LVT 的投递目标
            return Err(IrqError::InvalidArgument);
        }

        reg.write_common(entry.get_reg(), value & !LVT_MASK);

        Ok(())
    }

    fn check_entry(&self, entry: LvtEntry) -> Result<(), IrqError> {
        if self.supports(entry) {
            Ok(())
        } else {
            Err(IrqError::Unsupported)
        }
    }

    pub fn read_and_clear_error(&self) -> Result<u32, IrqError> {
        self.check_entry(LvtEntry::Error)?;
        let reg = self.reg();

        // 写 ESR 将内部错误状态锁存到可读寄存器，并重置内部状态
        reg.write_common(Esr, 0);

        Ok(reg.read_common(Esr))
    }

    /// 错误入口独立使用；不能再经过普通 flow 重复 EOI
    pub fn handle_error(&self) -> Result<u32, IrqError> {
        let error = self.read_and_clear_error()?;

        self.eoi();

        Ok(error)
    }

    /// 伪中断不设置 ISR，因此不能发送 EOI
    pub fn handle_spurious(&self) {}

    /// 读取 ISR 和 IRR 检查当前中断向量是否正忙
    pub(crate) fn is_busy(&self, vector: u8) -> bool {
        let reg = self.reg();
        let isr = reg.read_common(Isr(vector));
        let irr = reg.read_common(Irr(vector));

        let mask = 1 << (vector & 0x1f);

        isr & mask != 0 || irr & mask != 0
    }
}

impl LocalApic {
    pub fn id(&self) -> ApicId {
        match &self.lapic_type {
            LapicType::XApic(mmio) => ApicId::new(mmio.read_common(Id) >> 24 & 0xff),
            LapicType::X2Apic(msr) => ApicId::new(msr.read_common(Id)),
        }
    }

    /// 仅允许在屏蔽状态下修改向量，屏蔽位由 mask/unmask 单独控制
    pub fn set_lvt_entry(&self, entry: LvtEntry, vector: u8) -> Result<(), IrqError> {
        self.check_entry(entry)?;
        if vector < FIRST_DEVICE_VECTOR
            || vector == 0x80
            || vector == SPURIOUS_VECTOR
            || (entry == LvtEntry::Error) != (vector == ERROR_VECTOR)
        {
            return Err(IrqError::InvalidArgument);
        }
        let reg = self.reg();
        let value = reg.read_common(entry.get_reg());
        if value & LVT_MASK == 0 {
            return Err(IrqError::Busy);
        }
        reg.write_common(entry.get_reg(), (value & !0xff) | vector as u32);
        Ok(())
    }

    pub fn eoi(&self) {
        self.reg().write_common(Eoi, 0);
    }
}
