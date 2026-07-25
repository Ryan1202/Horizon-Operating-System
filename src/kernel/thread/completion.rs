use alloc::boxed::Box;
use core::{ffi::c_int, pin::Pin, ptr};

use crate::{
    kernel::{
        memory::{MemoryError, kmalloc::Kmalloc},
        thread::{
            WaitCondition, WaitQueue,
            wait_queue::{drop_raw, from_raw, into_raw},
        },
    },
    lib::rust::spinlock::{SpinIrqGuard, Spinlock},
};

/// 可计数的单次完成事件。
///
/// 每次 `complete` 发布一次完成，每次 `wait` 消费一次完成。
pub struct Completion {
    state: Spinlock<usize>,
    wait_queue: Pin<Box<WaitQueue, Kmalloc>>,
}

impl Completion {
    pub fn try_new() -> Result<Self, MemoryError> {
        Ok(Self {
            state: Spinlock::new(0),
            wait_queue: WaitQueue::try_new()?,
        })
    }

    /// 等待并消费一次完成。
    pub fn wait(&self) {
        let mut state = self.wait_queue.as_ref().wait(self);
        *state -= 1;
    }

    /// 发布一次完成，并至多唤醒一个等待线程。
    pub fn complete(&self) {
        let mut state = self.state.lock_irqsave();
        *state = state.saturating_add(1);
        self.wait_queue.as_ref().wake_one();
    }
}

impl WaitCondition for Completion {
    type State = usize;

    fn condition_lock(&self) -> &Spinlock<Self::State> {
        &self.state
    }

    fn is_satisfied(&self, guard: &SpinIrqGuard<'_, &mut Self::State>) -> bool {
        **guard != 0
    }
}

/// 布局固定、可由 C 直接内嵌的 Completion。
///
/// WaitQueue 的固定地址所有权以裸指针保存；C 只能通过 completion_* 接口
/// 操作该对象。
#[repr(C)]
pub struct CCompletion {
    state: Spinlock<usize>,
    wait_queue: *mut WaitQueue,
}

impl CCompletion {
    fn wait(&self) {
        let mut state = unsafe { from_raw(self.wait_queue) }.wait(self);
        *state -= 1;
    }

    fn complete(&self) {
        let mut state = self.state.lock_irqsave();
        *state = state.saturating_add(1);
        unsafe { from_raw(self.wait_queue) }.wake_one();
    }
}

impl WaitCondition for CCompletion {
    type State = usize;

    fn condition_lock(&self) -> &Spinlock<Self::State> {
        &self.state
    }

    fn is_satisfied(&self, guard: &SpinIrqGuard<'_, &mut Self::State>) -> bool {
        **guard != 0
    }
}

#[unsafe(export_name = "completion_init")]
extern "C" fn init_c(completion: *mut CCompletion) -> c_int {
    assert!(!completion.is_null(), "null CCompletion");
    let Ok(queue) = WaitQueue::try_new() else {
        return 0;
    };

    unsafe {
        completion.write(CCompletion {
            state: Spinlock::new(0),
            wait_queue: into_raw(queue),
        });
    }
    1
}

#[unsafe(export_name = "completion_deinit")]
extern "C" fn deinit_c(completion: *mut CCompletion) {
    let completion = unsafe { completion.as_mut() }.expect("null CCompletion");
    let queue = completion.wait_queue;
    completion.wait_queue = ptr::null_mut();
    unsafe { drop_raw(queue) };
}

#[unsafe(export_name = "completion_wait")]
extern "C" fn wait_c(completion: *const CCompletion) {
    unsafe { completion.as_ref() }
        .expect("null CCompletion")
        .wait();
}

#[unsafe(export_name = "completion_complete")]
extern "C" fn complete_c(completion: *const CCompletion) {
    unsafe { completion.as_ref() }
        .expect("null CCompletion")
        .complete();
}
