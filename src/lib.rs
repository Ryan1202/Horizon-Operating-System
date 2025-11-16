// Auto-generated root file for this crate
// Target: /Users/a15922/Horizon-Operating-System/src/arch/x86/i686-none.json
#![no_std]
#![no_main]
#![feature(sync_unsafe_cell)]

use core::panic::PanicInfo;

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
