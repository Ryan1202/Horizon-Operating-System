use core::{arch::asm, ptr::with_exposed_provenance};

use crate::{
    arch::x86::kernel::msr::IA32_GS_BASE,
    kernel::memory::percpu::{__percpu_start, CpuLocal, PerCpu, PerCpuInit, PerCpuReadWrite},
};

pub struct X86CpuLocal;

impl CpuLocal for X86CpuLocal {
    unsafe fn activate(base: *mut u8) -> usize {
        let addr = base.addr();

        // 如果溢出则取模，用于 gs:[offset] 时能恢复地址
        let delta = addr.wrapping_sub((&raw const __percpu_start).addr());

        unsafe {
            asm!(
                "wrmsr",
                in("ecx") IA32_GS_BASE,
                in("eax") (delta & 0xFFFF_FFFF) as u32,
                in("edx") (delta >> 32) as u32,
                options(nostack, preserves_flags),
            )
        }

        delta
    }

    unsafe fn get_ptr_for<T: PerCpuInit>(percpu: &PerCpu<T>, delta: usize) -> *const T {
        let addr = percpu.get_template().addr().wrapping_add(delta);
        with_exposed_provenance(addr)
    }
}

macro impl_percpu_scalar {
    ($(($ty:ty, $reg:ident, $size:literal $(, $modifier:literal)?)),+ $(,)?) => {
        $(
            impl crate::kernel::memory::percpu::PerCpuReadWrite<$ty> for crate::kernel::memory::percpu::PerCpu<$ty> {
                #[inline(always)]
                fn read(&self) -> $ty {
                    let value: $ty;
                    unsafe {
                        asm!(
                            concat!("mov {value", $(":", $modifier,)? "}, ", $size, " ptr gs:[{offset}]"),
                            value = out($reg) value,
                            offset = in(reg) self.get_template().addr(),
                            options(nostack, preserves_flags),
                        )
                    }
                    value
                }

                #[inline(always)]
                fn write(&self, value: $ty) {
                    unsafe {
                        asm!(
                            concat!("mov ", $size, " ptr gs:[{offset}], {value", $(":", $modifier,)? "}"),
                            offset = in(reg) self.get_template().addr(),
                            value = in($reg) value,
                            options(nostack, preserves_flags),
                        )
                    }
                }
            }

            impl crate::kernel::memory::percpu::PerCpuScalar<$ty> for crate::kernel::memory::percpu::PerCpu<$ty> {
                #[inline(always)]
                fn add(&self, value: $ty) {
                    unsafe {
                        asm!(
                            concat!("add ", $size, " ptr gs:[{offset}], {value", $(":", $modifier,)? "}"),
                            offset = in(reg) self.get_template().addr(),
                            value = in($reg) value,
                            options(nostack),
                        )
                    }
                }

                #[inline(always)]
                fn sub(&self, value: $ty) {
                    unsafe {
                        asm!(
                            concat!("sub ", $size, " ptr gs:[{offset}], {value", $(":", $modifier,)? "}"),
                            offset = in(reg) self.get_template().addr(),
                            value = in($reg) value,
                            options(nostack),
                        )
                    }
                }

                #[inline(always)]
                fn fetch_add(&self, value: $ty) -> $ty {
                    let mut result = value;
                    unsafe {
                        asm!(
                            concat!("xadd ", $size, " ptr gs:[{offset}], {value", $(":", $modifier,)? "}"),
                            offset = in(reg) self.get_template().addr(),
                            value = inout($reg) result,
                            options(nostack),
                        )
                    }
                    result
                }

                #[inline(always)]
                fn fetch_sub(&self, value: $ty) -> $ty {
                    let mut result = value.wrapping_neg();
                    unsafe {
                        asm!(
                            concat!("xadd ", $size, " ptr gs:[{offset}], {value", $(":", $modifier,)? "}"),
                            offset = in(reg) self.get_template().addr(),
                            value = inout($reg) result,
                            options(nostack),
                        )
                    }
                    result
                }

                #[inline(always)]
                fn increase(&self) {
                    unsafe {
                        asm!(
                            concat!("inc ", $size, " ptr gs:[{offset}]"),
                            offset = in(reg) self.get_template().addr(),
                            options(nostack),
                        )
                    }
                }

                #[inline(always)]
                fn decrease(&self) {
                    unsafe {
                        asm!(
                            concat!("dec ", $size, " ptr gs:[{offset}]"),
                            offset = in(reg) self.get_template().addr(),
                            options(nostack),
                        )
                    }
                }
            }
        )+
    },
}

impl_percpu_scalar!(
    (u8, reg_byte, "byte"),
    (i8, reg_byte, "byte"),
    (u16, reg, "word", "x"),
    (i16, reg, "word", "x"),
    (u32, reg, "dword", "e"),
    (i32, reg, "dword", "e"),
    (u64, reg, "qword", "r"),
    (i64, reg, "qword", "r"),
    (usize, reg, "qword", "r"),
    (isize, reg, "qword", "r"),
);

macro impl_percpu_pointer_read_write {
    ($ty:ty, $reg:ident, $size:literal $(, $modifier:literal)?) => {
        #[inline(always)]
        fn read(&self) -> $ty {
            let value: $ty;
            unsafe {
                asm!(
                    concat!("mov {value", $(":", $modifier,)? "}, ", $size, " ptr gs:[{offset}]"),
                    value = out($reg) value,
                    offset = in(reg) self.get_template().addr(),
                    options(nostack, preserves_flags),
                )
            }
            value
        }

        #[inline(always)]
        fn write(&self, value: $ty) {
            unsafe {
                asm!(
                    concat!("mov ", $size, " ptr gs:[{offset}], {value", $(":", $modifier,)? "}"),
                    offset = in(reg) self.get_template().addr(),
                    value = in($reg) value,
                    options(nostack, preserves_flags),
                )
            }
        }
    },
}

impl<T> PerCpuReadWrite<*const T> for PerCpu<*const T>
where
    *const T: PerCpuInit,
{
    impl_percpu_pointer_read_write!(*const T, reg, "qword", "r");
}

impl<T> PerCpuReadWrite<*mut T> for PerCpu<*mut T>
where
    *mut T: PerCpuInit,
{
    impl_percpu_pointer_read_write!(*mut T, reg, "qword", "r");
}
