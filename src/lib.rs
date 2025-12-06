// Auto-generated root file for this crate
// Target: /Users/a15922/Horizon-Operating-System/src/arch/x86/i686-none.json
#![no_std]
#![no_main]
#![feature(sync_unsafe_cell)]
#![feature(offset_of_enum)]
#![feature(pattern_type_range_trait)]

use core::panic::PanicInfo;

const CACHELINE_SIZE: usize = 64;

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

pub mod kernel {
    pub mod memory;
}
pub mod lib {
    pub mod rust;
}
