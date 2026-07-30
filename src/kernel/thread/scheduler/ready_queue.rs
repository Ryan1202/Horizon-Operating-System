use core::{pin::Pin, ptr::NonNull};

use crate::{
    kernel::thread::{Thread, ThreadState, core::ThreadError},
    lib::rust::list::ListHead,
};

pub struct ReadyQueue {
    head: ListHead<Thread>,
}

impl ReadyQueue {
    pub const fn new() -> Self {
        Self {
            head: ListHead::empty(),
        }
    }

    pub fn init(self: Pin<&mut Self>) {
        unsafe { self.get_unchecked_mut().head.init() };
    }

    pub unsafe fn enqueue(self: Pin<&mut Self>, thread: &Thread) -> Result<(), ThreadError> {
        thread.transition_to(ThreadState::Ready)?;
        unsafe { self.link(thread) };
        Ok(())
    }

    /// 仅当 thread 仍处于 Waking 时将其加入队列。
    ///
    /// 返回 false 表示另一个 CPU 已经完成入队或线程已经继续运行。
    pub unsafe fn enqueue_woken(self: Pin<&mut Self>, thread: &Thread) -> bool {
        if !thread.try_waking_to_ready() {
            return false;
        }

        unsafe { self.link(thread) };
        true
    }

    unsafe fn link(self: Pin<&mut Self>, thread: &Thread) {
        unsafe {
            self.get_unchecked_mut()
                .head
                .add_tail(thread.get_run_node())
        };
    }

    pub unsafe fn dequeue(self: Pin<&mut Self>, thread: &Thread) {
        unsafe { self.get_unchecked_mut().head.delete(thread.get_run_node()) };
    }

    pub fn next(self: Pin<&Self>) -> Option<NonNull<Thread>> {
        self.get_ref().head.iter(Thread::run_node_offset()).next()
    }
}
