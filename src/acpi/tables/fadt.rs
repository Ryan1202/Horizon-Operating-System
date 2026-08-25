use core::{num::NonZero, ptr::read_unaligned};

use crate::{
    acpi::tables::{DescriptionTable, GenericAddress, TableHeader},
    arch::PhysAddr,
};

pub const FADT_SIGNATURE: &[u8; 4] = b"FACP";

#[repr(C, packed)]
pub struct Fadt {
    header: TableHeader,
    firmware_ctrl: u32,
    dsdt: u32,
    reserved: u8,
    /// OEM 设置该字段用于指示偏好的电源管理配置
    preferred_pm_profile: u8,
    /// 表示在 8259 模式下的 SCI 中断向量号，
    /// 在没有 8259 的系统中表示 SCI 中断的 GSI 号
    sci_int: u16,
    /// SMI 命令端口的 I/O 地址
    smi_cmd: u32,
    /// ACPI 启用命令，将该值写入 `SMI_CMD` 端口以从 SMI 接管 ACPI
    acpi_enable: u8,
    /// ACPI 禁用命令，将该值写入 `SMI_CMD` 端口以将 ACPI 交还 SMI
    acpi_disable: u8,
    /// S4BIOS 请求命令，将该值写入 `SMI_CMD` 端口以请求固件进入 S4BIOS 状态
    s4bios_req: u8,
    /// P-State 控制命令，将该值写入 `SMI_CMD` 端口以接管 CPU 性能状态控制
    pstate_cnt: u8,
    pm1a_evt_blk: u32,
    pm1b_evt_blk: u32,
    pm1a_cnt_blk: u32,
    pm1b_cnt_blk: u32,
    pm2_cnt_blk: u32,
    pm_tmr_blk: u32,
    gpe0_blk: u32,
    gpe1_blk: u32,
    /// PM1 事件寄存器块长度，>= 4
    pm1_evt_len: u8,
    /// PM1 控制寄存器块长度，>= 2
    pm1_cnt_len: u8,
    /// PM2 控制寄存器块长度，>= 1
    pm2_cnt_len: u8,
    /// PM 定时器寄存器块长度（如果支持，== 4）
    pm_tmr_len: Option<NonZero<u8>>,
    /// GPE0 寄存器块长度，非零 2 的幂次方
    gpe0_blk_len: u8,
    /// GPE1 寄存器块长度，非零 2 的幂次方
    gpe1_blk_len: u8,
    /// 在 ACPI 通用寄存器里 GPE1 的偏移
    gpe1_base: u8,
    /// 如果不为 0，该值用于写入 `SMI_CMD` 用于指示
    /// OS 支持 `_CST` 对象和 Cx 状态切换通知
    cst_cnt: Option<NonZero<u8>>,
    /// 进入或退出 C2 状态所需的时间（微秒），>1000 表示不支持 C2 状态
    p_lvl2_lat: u16,
    /// 进入或退出 C3 状态所需的时间（微秒），>1000 表示不支持 C3 状态
    p_lvl3_lat: u16,
    /// 如果 CPU 不支持 WBINVD = 0，需要读取来刷新缓存的步数
    flush_size: u16,
    /// 如果 CPU 不支持 WBINVD = 0，需要读取来刷新缓存的步长
    flush_stride: u16,
    /// `P_CNT` 寄存器中的 `Duty Cycle` 位的偏移量
    duty_offset: u8,
    /// `P_CNT` 寄存器中的 `Duty Cycle` 位的宽度
    duty_width: u8,
    /// RTC 日闹钟的索引，如果不支持则为 0
    day_alrm: Option<NonZero<u8>>,
    /// RTC 月闹钟的索引，如果不支持则为 0
    mon_alrm: Option<NonZero<u8>>,
    /// RTC 的世纪寄存器的索引，如果不支持则为 0
    century: Option<NonZero<u8>>,
    /// IA 架构 PC 的启动标志
    iapc_boot_arch: u16,
    reserved2: u8,
    flags: u32,
    extend: FadtExtend,
}

#[allow(unused)]
#[repr(C, packed)]
pub struct FadtExtend {
    /// 重置寄存器的地址
    reset_reg: GenericAddress,
    /// 用于写入重置寄存器来重置系统的值
    reset_value: u8,
    arm_boot_arch: u16,
    minor_version: u8,
    /// `FACS` 的物理地址，必须优先尝试 `X_FIRMWARE_CTRL`，
    /// 否则使用 `FIRMWARE_CTRL` 的值
    x_firmware_ctrl: Option<NonZero<u64>>,
    /// `DSDT` 的物理地址，必须优先尝试 `X_DSDT`，否则使用 `DSDT` 的值
    x_dsdt: Option<NonZero<u64>>,
    /// PM1a 事件寄存器块的地址，必须优先尝试 `X_PM1a_EVT_BLK`，
    /// 否则使用 `PM1A_EVT_BLK` 的值
    x_pm1a_evt_blk: GenericAddress,
    /// PM1b 事件寄存器块的地址，必须优先尝试 `X_PM1b_EVT_BLK`，
    /// 否则使用 `PM1B_EVT_BLK` 的值
    x_pm1b_evt_blk: GenericAddress,
    /// PM1a 控制寄存器块的地址，必须优先尝试 `X_PM1a_CNT_BLK`，
    /// 否则使用 `PM1A_CNT_BLK` 的值
    x_pm1a_cnt_blk: GenericAddress,
    /// PM1b 控制寄存器块的地址，必须优先尝试 `X_PM1b_CNT_BLK`，
    /// 否则使用 `PM1B_CNT_BLK` 的值
    x_pm1b_cnt_blk: GenericAddress,
    /// PM2 控制寄存器块的地址，必须优先尝试 `X_PM2_CNT_BLK`，
    /// 否则使用 `PM2_CNT_BLK` 的值
    x_pm2_cnt_blk: GenericAddress,
    /// PM 定时器寄存器块的地址，必须优先尝试 `X_PM_TMR_BLK`，
    /// 否则使用 `PM_TMR_BLK` 的值
    x_pm_tmr_blk: GenericAddress,
    /// GPE0 寄存器块的地址，必须优先尝试 `X_GPE0_BLK`，否则使用 `GPE0_BLK` 的值
    x_gpe0_blk: GenericAddress,
    /// GPE1 寄存器块的地址，必须优先尝试 `X_GPE1_BLK`，否则使用 `GPE1_BLK` 的值
    x_gpe1_blk: GenericAddress,
    /// 睡眠控制寄存器的物理地址
    sleep_control_reg: GenericAddress,
    /// 睡眠状态寄存器的物理地址
    sleep_status_reg: GenericAddress,
    /// hypervisor 厂商的 ID
    hypervisor_vendor_identity: u64,
}

impl Fadt {
    /// 获取 IAPC 传统设备支持情况
    #[cfg(target_arch = "x86_64")]
    pub const fn get_iapc_capabilities(&self) -> IapcBootCapabilities {
        self.iapc_boot_arch.into()
    }

    const fn extend_info(&self) -> Option<&FadtExtend> {
        if self.header.length as usize == size_of::<Fadt>() {
            Some(&self.extend)
        } else {
            None
        }
    }

    pub fn dsdt(&self) -> PhysAddr {
        let dsdt = self
            .extend_info()
            .and_then(|e| unsafe { read_unaligned(&raw const e.x_dsdt) })
            .map_or(self.dsdt as usize, |x_dsdt| x_dsdt.get() as usize);
        PhysAddr::new(dsdt)
    }
}

impl DescriptionTable for Fadt {
    const SIGN: &[u8; 4] = FADT_SIGNATURE;
}

pub struct FadtFlag {
    wbinvd: bool,
    wbinvd_flush: bool,
    proc_c1: bool,
    p_lvl2_up: bool,
    pwr_button: bool,
    slp_button: bool,
    fix_rtc: bool,
    rtc_s4: bool,
    tmr_val_ext: bool,
    dck_cap: bool,
    reset_reg_sup: bool,
    sealed_case: bool,
    headless: bool,
    cpu_sw_slp: bool,
    pci_exp_wake: bool,
    use_platform_clock: bool,
    s4_rtc_sts_valid: bool,
    remote_power_on_capable: bool,
    force_apic_cluster_model: bool,
    force_apic_physical_dest_mode: bool,
    hardware_reduced: bool,
    low_power_idle_capable: bool,
    persistent_cpu_caches: bool,
}

pub struct IapcBootCapabilities {
    pub legacy_devices: bool,
    pub i8042: bool,
    pub vga: bool,
    pub msi: bool,
    pub pcie_aspm: bool,
    pub rtc: bool,
}

const impl From<u16> for IapcBootCapabilities {
    fn from(value: u16) -> Self {
        Self {
            legacy_devices: value & 0x1 != 0,
            i8042: value & 0x2 != 0,
            vga: value & 0x4 == 0,
            msi: value & 0x8 == 0,
            pcie_aspm: value & 0x10 == 0,
            rtc: value & 0x20 == 0,
        }
    }
}
