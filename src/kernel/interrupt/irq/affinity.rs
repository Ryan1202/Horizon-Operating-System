use crate::kernel::topology::ThinCpuMask;

pub type Mask = ThinCpuMask;

pub struct Affinity {
    pub(super) requested: Mask,
    pub(super) effective: Mask,
}

const impl Default for Affinity {
    fn default() -> Self {
        let requested = Mask::new_full().expect("Failed to create requested affinity mask");
        let effective = Mask::new_zeroed().expect("Failed to create effective affinity mask");
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
            effective: Mask::new_zeroed().expect("Failed to create effective affinity mask"),
        }
    }

    pub const fn requested(&self) -> &Mask {
        &self.requested
    }

    pub const fn effective(&self) -> &Mask {
        &self.effective
    }
}
