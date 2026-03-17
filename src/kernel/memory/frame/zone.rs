use crate::arch::PhysAddr;

/// 内存区域类型
/// Zone只决定了系统能管理的物理内存范围，与Page管理的内存范围无关
#[repr(u8)]
#[derive(Clone, Copy, Debug)]
pub enum ZoneType {
    /// 内核线性映射区：32位下低于896MB，64位下高于4GB
    LinearMem = 0,
    /// 32位可寻址内存：
    /// 32位下为高于线性映射区的内存，64位下为32位地址范围内内存
    MEM32 = 1,
}

pub const ZONE_COUNT: usize = 2;

impl ZoneType {
    pub const fn index(&self) -> usize {
        match self {
            #[cfg(target_pointer_width = "32")]
            ZoneType::LinearMem => 0,
            #[cfg(target_pointer_width = "32")]
            ZoneType::MEM32 => 1,

            #[cfg(target_pointer_width = "64")]
            ZoneType::MEM32 => 0,
            #[cfg(target_pointer_width = "64")]
            ZoneType::LinearMem => 1,
        }
    }

    pub const fn from_index(index: usize) -> Self {
        #[cfg(target_pointer_width = "32")]
        {
            match index {
                0 => ZoneType::LinearMem,
                1 => ZoneType::MEM32,
                _ => panic!("Invalid zone index"),
            }
        }

        #[cfg(target_pointer_width = "64")]
        {
            match index {
                0 => ZoneType::MEM32,
                1 => ZoneType::LinearMem,
                _ => panic!("Invalid zone index"),
            }
        }
    }

    pub const fn range(&self) -> (PhysAddr, PhysAddr) {
        match self {
            #[cfg(target_pointer_width = "32")]
            ZoneType::LinearMem => (PhysAddr::new(0x100000), PhysAddr::new(0x30000000)),
            #[cfg(target_pointer_width = "64")]
            ZoneType::LinearMem => (PhysAddr::new(1 << 32), PhysAddr::new(usize::MAX)),

            #[cfg(target_pointer_width = "32")]
            ZoneType::MEM32 => (PhysAddr::new(0x30000000), PhysAddr::new(usize::MAX)),
            #[cfg(target_pointer_width = "64")]
            ZoneType::MEM32 => (PhysAddr::new(0), PhysAddr::new(1 << 32)),
        }
    }

    pub const fn from_address(addr: PhysAddr) -> Self {
        assert!(
            addr.to_frame_number().get() >= 0x100,
            "Low 1MiB memory is reserved"
        );
        #[cfg(target_pointer_width = "32")]
        {
            if addr.to_frame_number().get() < 0x30000 {
                ZoneType::LinearMem
            } else {
                ZoneType::MEM32
            }
        }

        #[cfg(target_pointer_width = "64")]
        {
            if addr.to_frame_number().get() < 0x100000 {
                ZoneType::MEM32
            } else {
                ZoneType::LinearMem
            }
        }
    }
}
