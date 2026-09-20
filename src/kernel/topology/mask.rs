use crate::{
    kernel::topology::{CpuId, NR_CPUS_MAX},
    lib::rust::bitset::BitSet,
};

const CPU_MASK_COUNTS: usize = (NR_CPUS_MAX / usize::BITS) as usize;
type Static = [usize; CPU_MASK_COUNTS];

#[derive(Clone)]
pub struct ThinCpuMask {
    bits: Static,
}

#[derive(Clone)]
pub struct CpuMask {
    bits: BitSet<Static>,
}

impl From<ThinCpuMask> for CpuMask {
    fn from(thin: ThinCpuMask) -> Self {
        Self {
            bits: BitSet::from_storage(thin.bits, NR_CPUS_MAX as usize),
        }
    }
}

impl From<&ThinCpuMask> for CpuMask {
    fn from(thin: &ThinCpuMask) -> Self {
        Self {
            bits: BitSet::from_storage(thin.bits, NR_CPUS_MAX as usize),
        }
    }
}

impl From<CpuMask> for ThinCpuMask {
    fn from(mask: CpuMask) -> Self {
        Self {
            bits: mask.bits.breakdown().0,
        }
    }
}

impl ThinCpuMask {
    pub const fn new(bits: Static) -> Option<Self> {
        Some(Self { bits })
    }

    pub fn contains(&self, cpu: CpuId) -> bool {
        BitSet::from_storage(&self.bits, NR_CPUS_MAX as usize).test(cpu.get() as usize)
    }
}

impl ThinCpuMask {
    pub fn set(&mut self, cpu: CpuId) {
        BitSet::from_storage(&mut self.bits, NR_CPUS_MAX as usize).set(cpu.get() as usize);
    }

    pub fn clear(&mut self, cpu: CpuId) {
        BitSet::from_storage(&mut self.bits, NR_CPUS_MAX as usize).clear(cpu.get() as usize);
    }

    pub fn intersect(&mut self, other: &Self) -> bool {
        BitSet::from_storage(&mut self.bits, NR_CPUS_MAX as usize)
            .intersect(&BitSet::from_storage(&other.bits, NR_CPUS_MAX as usize))
    }

    pub fn copy_from(&mut self, other: &CpuMask) {
        let other_word = other.bits.as_ref();
        for (index, word) in self.bits.as_mut().iter_mut().enumerate() {
            *word = other_word.get(index).copied().unwrap_or(0);
        }
    }
}

impl CpuMask {
    pub fn new(bits: Static, len: usize) -> Option<Self> {
        let bits = BitSet::new(bits, len)?;
        Some(Self { bits })
    }

    pub fn contains(&self, cpu: CpuId) -> bool {
        self.bits.test(cpu.get() as usize)
    }

    pub fn set(&mut self, cpu: CpuId) {
        self.bits.set(cpu.get() as usize);
    }

    pub fn clear(&mut self, cpu: CpuId) {
        self.bits.clear(cpu.get() as usize);
    }

    pub fn intersect(&mut self, other: &CpuMask) -> bool {
        self.bits.intersect(&other.bits)
    }
}

impl ThinCpuMask {
    pub const fn new_zeroed() -> Option<Self> {
        Some(Self {
            bits: [0; CPU_MASK_COUNTS],
        })
    }

    pub const fn new_full() -> Option<Self> {
        Some(Self {
            bits: [usize::MAX; CPU_MASK_COUNTS],
        })
    }
}

impl CpuMask {
    pub const fn new_zeroed() -> Option<Self> {
        Some(Self {
            bits: BitSet::zeroed(NR_CPUS_MAX as usize),
        })
    }

    pub fn new_full() -> Option<Self> {
        Some(Self {
            bits: BitSet::new([usize::MAX; CPU_MASK_COUNTS], NR_CPUS_MAX as usize).unwrap(),
        })
    }
}
