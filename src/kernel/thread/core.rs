use core::{ffi::c_void, ptr::NonNull};

use crate::{
    arch::ArchPageTable,
    kernel::{
        memory::{
            MemoryError,
            arch::ArchMemory,
            frame::buddy::FrameOrder,
            page::{Pages, options::PageAllocOptions},
        },
        thread::scheduler::scheduler,
    },
};

mod id;
mod state;
mod thread;

pub use id::ThreadId;
pub use state::ThreadState;
pub use thread::Thread;

const THREAD_STACK_ORDER: FrameOrder = FrameOrder::new(2);

pub type KernelThreadEntry = extern "C" fn(*mut c_void);

/// 架构层必须提供的最小线程上下文接口
///
/// 寄存器布局、初始 switch frame 和 trampoline 均由架构实现私有管理。线程核心
/// 只提供一段已分配的内核栈，并保存返回的 opaque context
pub trait ThreadContext: Sized {
    /// 在 `stack_bottom..stack_bottom + stack_size` 中构造新内核线程的初始帧
    ///
    /// # Safety
    ///
    /// 栈范围必须独占、可写，并且至少在线程对象存活期间保持有效
    unsafe fn new_kernel(
        stack: &mut KernelStack,
        entry: KernelThreadEntry,
        argument: *mut c_void,
    ) -> Self;

    /// 保存当前上下文并恢复 `next`
    ///
    /// # Safety
    ///
    /// 当前 CPU 必须独占两个上下文，且满足架构切换所需的中断和抢占约束
    unsafe fn switch_to(&mut self, next: &Self);

    /// 为第一个线程构造初始上下文。
    ///
    /// # Safety
    ///
    /// 只能在构造第一个线程时使用，否则会破坏当前线程的上下文
    unsafe fn prepare_first_thread(context: &Self);
}

pub extern "C" fn thread_entry_wrapper(entry: KernelThreadEntry, argument: *mut c_void) -> ! {
    let scheduler = scheduler();
    unsafe { scheduler.finish_first_switch() };
    entry(argument);
    scheduler.exit_self()
}

#[derive(Debug)]
pub enum ThreadError {
    InvalidTransition { from: ThreadState, to: ThreadState },
}

#[repr(transparent)]
pub struct KernelStack {
    pages: Pages,
}

unsafe impl Sync for KernelStack {}
unsafe impl Send for KernelStack {}

impl KernelStack {
    fn new() -> Result<Self, MemoryError> {
        let pages = PageAllocOptions::kernel(THREAD_STACK_ORDER)
            .zeroed(true)
            .allocate()?;

        Ok(Self { pages })
    }

    pub fn top(&self) -> NonNull<u8> {
        let bottom = self.bottom();
        unsafe { bottom.byte_add(self.size()) }
    }

    pub fn bottom(&self) -> NonNull<u8> {
        self.pages.get_ptr()
    }

    const fn size(&self) -> usize {
        self.pages.get_count() * ArchPageTable::PAGE_SIZE
    }
}
