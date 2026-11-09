use crate::kernel::topology::ThinCpuMask;

pub type Mask = ThinCpuMask;

pub struct Affinity {
    pub(super) requested: Mask,
    pub(super) effective: Mask,
}

const impl Default for Affinity {
    fn default() -> Self {
        let requested = Mask::new_full();
        let effective = Mask::new_zeroed();
        Self {
            requested,
            effective,
        }
    }
}

impl Affinity {
    pub const fn new(requested: Mask) -> Self {
        Self {
            requested,
            effective: Mask::new_zeroed(),
        }
    }

    pub const fn requested(&self) -> &Mask {
        &self.requested
    }

    pub const fn effective(&self) -> &Mask {
        &self.effective
    }
}
