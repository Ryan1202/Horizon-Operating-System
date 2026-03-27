// Auto-generated root file for this crate
// Target: /Users/a15922/Horizon-Operating-System/src/arch/x86/i686-none.json
// Based on: "/Users/a15922/Horizon-Operating-System/src/root.template.rs"

#![no_std]
#![no_main]
#![feature(sync_unsafe_cell)]
#![feature(const_cmp)]
#![feature(const_trait_impl)]
#![feature(const_try)]
#![feature(const_result_trait_fn)]
#![feature(atomic_ptr_null)]

use core::{fmt, panic::PanicInfo};

pub mod arch;

const CACHELINE_SIZE: usize = 64;

unsafe extern "C" {
    fn printk(fmt: *const u8, va_args: ...) -> i32;
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

#[macro_export]
macro_rules! printk {
    ($($arg:tt)*) => {{
        use core::fmt::Write;

        let mut output = crate::ConsoleOutput;
        let _ = write!(output, $($arg)*);
    }};
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    printk!("Kernel Panic: {}\n", _info);
    loop {}
}

// Auto-generated module declarations
pub mod lib {
    pub mod rust;
}
pub mod kernel {
    pub mod memory;
}
