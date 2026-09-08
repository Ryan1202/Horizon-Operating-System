//! IRQ core 私有的阻塞管理锁与条件等待
use super::IrqError;
use crate::{
    kernel::{
        interrupt,
        memory::kmalloc::Kmalloc,
        thread::{
            WaitCondition, WaitQueue,
            scheduler::{self, PreemptGuard},
        },
    },
    lib::rust::spinlock::{SpinIrqGuard, Spinlock},
};
use alloc::boxed::Box;
use core::pin::Pin;

pub(super) fn assert_management() {
    assert!(
        interrupt::in_thread(),
        "IRQ management from interrupt/handler context"
    );
    scheduler::assert_can_wait();
}

pub(super) struct ManagementLock {
    held: Spinlock<bool>,
    queue: Pin<Box<WaitQueue, Kmalloc>>,
}

pub(super) struct ManagementGuard<'a>(&'a ManagementLock);

impl !Send for ManagementGuard<'_> {}
impl !Sync for ManagementGuard<'_> {}

impl ManagementLock {
    pub fn new() -> Result<Self, IrqError> {
        Ok(Self {
            held: Spinlock::new(false),
            queue: WaitQueue::try_new()?,
        })
    }

    pub fn lock(&self) -> ManagementGuard<'_> {
        assert_management();
        let mut held = self.queue.wait(self);
        *held = true;
        ManagementGuard(self)
    }
}

impl WaitCondition for ManagementLock {
    type State = bool;

    fn condition_lock(&self) -> &Spinlock<bool> {
        &self.held
    }

    fn is_satisfied(&self, guard: &SpinIrqGuard<'_, &mut bool>) -> bool {
        !**guard
    }
}

impl Drop for ManagementGuard<'_> {
    fn drop(&mut self) {
        let preempt = PreemptGuard::new();
        let mut held = self.0.held.lock_irqsave();

        *held = false;

        self.0.queue.wake_one(&preempt);
    }
}
