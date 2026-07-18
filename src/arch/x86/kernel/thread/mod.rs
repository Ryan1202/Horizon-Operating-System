use core::{
    arch::{asm, naked_asm},
    mem::size_of,
};

use crate::kernel::thread::core::{
    KernelStack, KernelThreadEntry, ThreadContext, thread_entry_wrapper,
};

/// x86_64 的长期线程上下文。
///
/// 由本模块协议保证跨切换保留的寄存器位于内核栈上的 `X86SwitchFrame` 中，
/// 因此线程控制块只需要记录下一次恢复时使用的栈指针。调用参数寄存器属于
/// 一次性 trampoline，不属于长期上下文。
#[repr(transparent)]
pub struct X86ThreadContext {
    stack_pointer: usize,
}

/// `x86_switch_to` 恢复的栈帧。
///
/// 字段顺序必须与汇编中的 pop 顺序完全一致。`return_address` 由最后的 `ret`
/// 取出，新线程第一次运行时指向 `x86_kernel_thread_entry`。
#[repr(C)]
struct X86SwitchFrame {
    r15: usize,
    r14: usize,
    r13: usize,
    r12: usize,
    rbx: usize,
    rbp: usize,
    return_address: usize,
}

impl ThreadContext for X86ThreadContext {
    unsafe fn new_kernel(
        stack: &mut KernelStack,
        entry: KernelThreadEntry,
        argument: *mut core::ffi::c_void,
    ) -> Self {
        let stack_top = stack.top();

        let frame = unsafe { stack_top.sub(size_of::<X86SwitchFrame>()) }.cast::<X86SwitchFrame>();

        // SAFETY: `frame` 位于当前线程独占的内核栈范围内，按 usize 对齐，且在
        // 线程首次被调度前不会被读取。
        unsafe {
            frame.write(X86SwitchFrame {
                r15: 0,
                r14: 0,
                r13: argument as usize,
                r12: entry as usize,
                rbx: 0,
                rbp: 0,
                return_address: x86_kernel_thread_entry as *const () as usize,
            });
        }

        Self {
            stack_pointer: frame.addr().get(),
        }
    }

    unsafe fn switch_to(&mut self, next: &Self) {
        let current_stack_pointer = &mut self.stack_pointer as *mut usize;
        let next_stack_pointer = &next.stack_pointer as *const usize;

        unsafe {
            // 未进入 X86SwitchFrame 的通用寄存器全部显式声明为clobber；
            // RFLAGS 也使用 asm! 的默认 clobber 语义。
            asm!(
                "call {switch}",
                switch = sym x86_switch_to,
                inlateout("rdi") current_stack_pointer => _,
                inlateout("rsi") next_stack_pointer => _,
                lateout("rax") _,
                lateout("rcx") _,
                lateout("rdx") _,
                lateout("r8") _,
                lateout("r9") _,
                lateout("r10") _,
                lateout("r11") _,
            )
        }
    }

    unsafe fn prepare_first_thread(context: &X86ThreadContext) {
        unsafe {
            asm!(
                "mov rsp, {stack}",
                "pop r15",
                "pop r14",
                "pop r13",
                "pop r12",
                "pop rbx",
                "pop rbp",
                "ret",
                stack = in(reg) context.stack_pointer,
                options(noreturn),
            );
        }
    }
}

/// 保存当前协议要求保留的寄存器并恢复下一个线程。
///
/// # Safety
///
/// 调用者必须将可写的当前栈指针地址放入 `rdi`，将由 `X86SwitchFrame` 初始化
/// 或由本函数之前保存的下一个栈指针地址放入 `rsi`，并保证切换期间不会并发
/// 修改上下文。该寄存器协议属于 x86 线程模块，不依赖平台 C ABI。
#[unsafe(naked)]
unsafe extern "custom" fn x86_switch_to() {
    naked_asm!(
        "push rbp",
        "push rbx",
        "push r12",
        "push r13",
        "push r14",
        "push r15",
        "mov [rdi], rsp",
        "mov rsp, [rsi]",
        "pop r15",
        "pop r14",
        "pop r13",
        "pop r12",
        "pop rbx",
        "pop rbp",
        "ret",
    )
}

#[cfg(all(target_arch = "x86_64"))]

/// 新内核线程的一次性入口。
///
/// `r12` 和 `r13` 只存在于初始 switch frame：分别保存入口函数和参数。线程
/// wrapper 完成首次切换收尾、开启中断并执行线程函数；线程函数返回后由
/// wrapper 进入不返回的调度器退出路径。
#[cfg(all(target_arch = "x86_64", not(target_os = "windows")))]
#[unsafe(naked)]
unsafe extern "custom" fn x86_kernel_thread_entry() -> ! {
    naked_asm!(
        // 线程函数在 r12 中，调用约定要求第一个参数在 rdi 中。
        "mov rdi, r12",
        // 线程函数的参数在 r13 中，调用约定要求第一个参数在 rdi 中。
        "mov rsi, r13",
        "call {wrapper}",
        "ud2",
        wrapper = sym thread_entry_wrapper,
    )
}
