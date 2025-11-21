mod kmalloc;
mod page;
mod slub;

#[repr(C)]
pub struct E820Ards {
    pub base_addr: u64,
    pub length: u64,
    pub block_type: u32,
}

#[unsafe(no_mangle)]
pub static mut PREALLOCATED_END_PHY: usize = 0;
