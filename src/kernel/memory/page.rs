pub trait PageTableEntry {
    type PhyAddr;

    const MAX: usize;

    fn new_absent() -> Self;

    fn new_present(phy_addr: Self::PhyAddr) -> Self;

    fn is_present(&self) -> bool;

    fn phy_addr(&self) -> Self::PhyAddr;
}
