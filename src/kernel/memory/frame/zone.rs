#[cfg(target_pointer_width = "32")]
use crate::arch::PhyAddr;

/// 内存区域类型
/// Zone只决定了系统能管理的物理内存范围，与Page管理的内存范围无关
#[repr(u8)]
#[derive(Clone, Copy, Debug)]
pub enum ZoneType {
    /// (ISA )DMA区，低于16MB
    MEM24 = 0,
    /// 内核线性映射区，低于4GB（64位）或512MB（32位）
    LinearMem = 1,
    /// 高端内存区，超过4GB（64位）或512MB（32位）
    HighMem = 2,
}

pub const ZONE_COUNT: usize = 3;

impl ZoneType {
    pub const fn index(&self) -> usize {
        match self {
            ZoneType::MEM24 => 0,
            ZoneType::LinearMem => 1,
            ZoneType::HighMem => 2,
        }
    }

    pub const fn from_index(index: usize) -> Self {
        match index {
            0 => ZoneType::MEM24,
            1 => ZoneType::LinearMem,
            2 => ZoneType::HighMem,
            _ => panic!("Invalid zone index"),
        }
    }

    pub const fn range(&self) -> (PhyAddr, PhyAddr) {
        match self {
            ZoneType::MEM24 => (PhyAddr::new(0), PhyAddr::new((1 << 24) - 1)), // 16MB
            #[cfg(target_pointer_width = "32")]
            ZoneType::LinearMem => (PhyAddr::new(1 << 24), PhyAddr::new(0x1fffffff)), // 512MB
            #[cfg(target_pointer_width = "64")]
            ZoneType::LinearMem => (PhyAddr::new(1 << 24), PhyAddr::new(1 << 32)), // 4GB
            #[cfg(target_pointer_width = "32")]
            ZoneType::HighMem => (PhyAddr::new(0x20000000), PhyAddr::new(usize::MAX)), // >512MB
            #[cfg(target_pointer_width = "64")]
            ZoneType::HighMem => (PhyAddr::new(1 << 32), PhyAddr::new(usize::MAX)), // >4GB
        }
    }

    pub const fn from_address(addr: PhyAddr) -> Self {
        match (addr.as_usize() | 1).ilog2() {
            0..24 => ZoneType::MEM24,
            #[cfg(target_pointer_width = "32")]
            24..29 => ZoneType::LinearMem,
            #[cfg(target_pointer_width = "64")]
            24..32 => ZoneType::LinearMem,
            _ => ZoneType::HighMem,
        }
    }
}
