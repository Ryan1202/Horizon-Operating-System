#![no_std]
#![no_main]
#![feature(sync_unsafe_cell)]
#![feature(offset_of_enum)]
#![feature(pattern_type_range_trait)]

use core::{
    fmt::{self, Write},
    panic::PanicInfo,
};

const CACHELINE_SIZE: usize = 64;

unsafe extern "C" {
    fn printk(fmt: *const u8) -> i32;
}

pub struct ConsoleOutput;
impl fmt::Write for ConsoleOutput {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        unsafe {
            let buf = [0u8; 256];
            let len = s.len().min(255);
            core::ptr::copy_nonoverlapping(s.as_ptr(), buf.as_ptr() as *mut u8, len);
            printk(buf.as_ptr());
        }
        Ok(())
    }
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    let mut panic_output = ConsoleOutput;
    let _ = writeln!(panic_output, "Kernel Panic: {}", _info);
    loop {}
}
