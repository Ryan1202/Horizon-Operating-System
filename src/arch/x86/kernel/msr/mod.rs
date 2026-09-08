macro_rules! define_msr {
    ($($name:ident = $value:expr),+ $(,)?) => {
        $(
            #[allow(unused)]
            pub const ${concat(IA32_, $name)}: u32 = $value;
        )+
    };
}

mod architectural;

use core::arch::asm;

pub use architectural::*;

/// 读取模型特定寄存器
///
/// # Safety
///
/// `msr` 必须是有效的寄存器索引，且当前 CPU 支持该 MSR
#[inline(always)]
pub unsafe fn rdmsr(msr: u32) -> u64 {
    let mut low: u32;
    let mut high: u32;

    unsafe {
        asm! {
            "rdmsr",
            in("ecx") msr,
            out("eax") low,
            out("edx") high,
            options(nomem, nostack, preserves_flags)
        }
    };

    ((high as u64) << 32) | (low as u64)
}

/// 写入模型特定寄存器
///
/// # Safety
///
/// `msr` 必须是可写的，且写入的值合法
#[inline(always)]
pub unsafe fn wrmsr(msr: u32, value: u64) {
    let low = value as u32;
    let high = (value >> 32) as u32;

    unsafe {
        asm! {
            "wrmsr",
            in("ecx") msr,
            in("eax") low,
            in("edx") high,
            options(nomem, nostack, preserves_flags)
        }
    };
}
