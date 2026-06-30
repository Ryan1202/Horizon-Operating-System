use crate::arch::PhysAddr;

#[repr(C)]
#[derive(Clone)]
pub struct Constraints {
    pub mask: usize,
    pub coherent_mask: usize,
    pub boundary_mask: usize,
    pub max_segment_size: u32,
    pub max_segments: u16,
}

impl Constraints {
    pub const fn new(
        mask: usize,
        coherent_mask: usize,
        boundary_mask: usize,
        max_segment_size: u32,
        max_segments: u16,
    ) -> Self {
        Constraints {
            mask,
            coherent_mask,
            boundary_mask,
            max_segment_size,
            max_segments,
        }
    }

    pub fn is_coherent_satisfied(&self, addr: PhysAddr) -> bool {
        (addr.as_usize() & !self.coherent_mask) == 0
    }

    /// 检查 [addr, addr+size) 是否跨越 boundary 边界。
    /// 返回 true 表示跨越（无效）。
    pub fn crosses_boundary(&self, addr: PhysAddr, size: usize) -> bool {
        if self.boundary_mask == 0 || size == 0 {
            return false;
        }
        let Some(end) = addr.as_usize().checked_add(size - 1) else {
            return true;
        };
        (addr.as_usize() & !self.boundary_mask) != (end & !self.boundary_mask)
    }

    /// 检查单段大小是否超过 max_segment_size。
    /// 返回 true 表示超限（无效）。
    pub fn exceeds_max_segment_size(&self, size: usize) -> bool {
        self.max_segment_size > 0 && size > self.max_segment_size as usize
    }
}
