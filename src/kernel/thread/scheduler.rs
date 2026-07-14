use core::{pin::Pin, ptr::NonNull};

use crate::{
    kernel::thread::{Thread, ThreadState, core::ThreadError},
    lib::rust::{list::ListHead, spinlock::Spinlock},
};

#[repr(transparent)]
pub struct Scheduler {
    inner: Spinlock<SchedulerInner>,
}

pub struct SchedulerInner {
    ready: ListHead<Thread>,
}

impl Scheduler {
    pub const fn new() -> Self {
        Self {
            inner: Spinlock::new(SchedulerInner::new()),
        }
    }

    pub(super) fn init(&self) {
        self.inner.lock().init();
    }

    /// 允许线程被调度，如果该状态变化不合法，则返回错误。
    pub fn enqueue(&self, thread: &Thread) -> Result<(), ThreadError> {
        let mut inner = self.inner.lock();
        unsafe { inner.enqueue(thread) }
    }

    /// 禁止线程被调度，如果该状态变化不合法，则返回错误。
    pub fn dequeue(&self, thread: &Thread, state: ThreadState) -> Result<(), ThreadError> {
        let mut inner = self.inner.lock();
        unsafe { inner.dequeue(thread, state) }
    }

    pub fn next_eligible(&self) -> Option<NonNull<Thread>> {
        self.inner.lock().next_eligible()
    }
}

impl SchedulerInner {
    const fn new() -> Self {
        Self {
            ready: ListHead::empty(),
        }
    }

    fn init(&mut self) {
        unsafe { Pin::new_unchecked(&mut self.ready).init() };
    }

    unsafe fn ready_head(&mut self) -> Pin<&mut ListHead<Thread>> {
        unsafe { Pin::new_unchecked(&mut self.ready) }
    }

    /// 允许线程被调度，如果该状态变化不合法，则返回错误。
    pub unsafe fn enqueue(&mut self, thread: &Thread) -> Result<(), ThreadError> {
        thread.transition_to(ThreadState::Ready)?;
        unsafe { self.ready_head().add_tail(thread.get_node()) };
        Ok(())
    }

    /// 禁止线程被调度，如果该状态变化不合法，则返回错误。
    pub unsafe fn dequeue(
        &mut self,
        thread: &Thread,
        state: ThreadState,
    ) -> Result<(), ThreadError> {
        match thread.transition_to(state) {
            Ok(_) => unsafe {
                self.ready_head().delete(thread.get_node());
                Ok(())
            },
            e => e,
        }
    }

    fn next_eligible(&mut self) -> Option<NonNull<Thread>> {
        unsafe { self.ready_head() }
            .iter(Thread::list_offset())
            .next()
    }
}
