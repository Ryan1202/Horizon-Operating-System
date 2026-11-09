use core::arch::asm;

macro_rules! io_rw {
    ($in_name:ident, $out_name:ident, $ty:ty, $reg:tt) => {
        #[inline]
        pub unsafe fn $in_name(port: u16) -> $ty {
            let value: $ty;
            unsafe {
                asm!(
                    concat!("in ", $reg, ", dx"),
                    in("dx") port,
                    out($reg) value,
                    options(nostack),
                );
            }
            value
        }

        #[inline]
        pub unsafe fn $out_name(port: u16, value: $ty) {
            unsafe {
                asm!(
                    concat!("out dx, ", $reg),
                    in("dx") port,
                    in($reg) value,
                    options(nostack),
                );
            }
        }
    };
}

io_rw!(io_in8, io_out8, u8, "al");
io_rw!(io_in16, io_out16, u16, "ax");
io_rw!(io_in32, io_out32, u32, "eax");
io_rw!(io_in64, io_out64, u64, "rax");
