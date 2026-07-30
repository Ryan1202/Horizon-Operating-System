use core::ffi::c_int;

use crate::{
    kernel::thread::{WaitCondition, WaitQueue},
    lib::rust::spinlock::{SpinIrqGuard, Spinlock},
};

/// 可计数的单次完成事件。
///
/// 每次 `complete` 发布一次完成，每次 `wait` 消费一次完成。
#[repr(C)]
pub struct Completion {
    wait_queue: WaitQueue,
    state: Spinlock<usize>,
}

impl Completion {
    pub const fn new() -> Self {
        Self {
            state: Spinlock::new(0),
            wait_queue: WaitQueue::new_uninit(),
        }
    }

    pub fn init(&self) {
        self.wait_queue.init();
    }

    /// 等待并消费一次完成。
    pub fn wait(&self) {
        let mut state = self.wait_queue.wait(self);
        *state -= 1;
    }

    /// 发布一次完成，并至多唤醒一个等待线程。
    pub fn complete(&self) {
        let mut state = self.state.lock_irqsave();
        *state = state.saturating_add(1);
        self.wait_queue.wake_one();
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

#[unsafe(export_name = "completion_init")]
extern "C" fn init_c(completion: *mut Completion) -> c_int {
    assert!(!completion.is_null(), "null Completion from C");

    unsafe {
        completion.write(Completion {
            state: Spinlock::new(0),
            wait_queue: WaitQueue::new_uninit(),
        });

        completion.as_mut().unwrap().wait_queue.init();
    }
    1
}

#[unsafe(export_name = "completion_wait")]
extern "C" fn wait_c(completion: *const Completion) {
    unsafe { completion.as_ref() }
        .expect("null Completion from C")
        .wait();
}

#[unsafe(export_name = "completion_complete")]
extern "C" fn complete_c(completion: *const Completion) {
    unsafe { completion.as_ref() }
        .expect("null Completion from C")
        .complete();
}
