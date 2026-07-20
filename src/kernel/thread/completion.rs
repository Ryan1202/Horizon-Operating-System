use alloc::boxed::Box;
use core::pin::Pin;

use crate::{
    kernel::{
        memory::{MemoryError, kmalloc::Kmalloc},
        thread::{WaitCondition, WaitQueue},
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
