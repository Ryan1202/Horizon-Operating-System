use alloc::boxed::Box;
use core::{
    cell::SyncUnsafeCell,
    ffi::{c_int, c_void},
    field::field_of,
    pin::Pin,
    ptr::NonNull,
    sync::atomic::Ordering,
};

use crate::{
    kernel::{
        interrupt::{self},
        memory::{MemoryError, kmalloc::Kmalloc},
        thread::{
            Thread, ThreadState,
            scheduler::{
                PreemptGuard, Scheduler, can_preempt, request_reschedule, scheduler,
                scheduler_unchecked,
            },
        },
    },
    lib::rust::{
        list::{ListHead, ListNode},
        spinlock::{SpinIrqGuard, Spinlock},
    },
};

/// 一次线程等待所使用的侵入式队列成员。
///
/// Waiter 固定内嵌于 Thread，但等待队列只管理 Waiter，不直接管理 Thread
/// 的运行队列节点。当前阶段一条线程同时只能进入一个等待队列。
pub struct Waiter {
    #[allow(unused)]
    node: SyncUnsafeCell<ListNode<Waiter>>,
}

impl Waiter {
    pub(super) const fn new() -> Self {
        Self {
            node: SyncUnsafeCell::new(ListNode::new()),
        }
    }

    /// 调用方必须持有 Waiter 所在 WaitQueue 的锁。
    pub(super) fn is_linked(&self) -> bool {
        unsafe { &*self.node.get() }.is_linked()
    }
}

/// 描述由某把自旋锁保护的等待条件。
pub trait WaitCondition {
    type State: Unpin;

    /// 返回保护条件状态的锁。
    fn condition_lock(&self) -> &Spinlock<Self::State>;

    /// 在持有条件锁时判断条件是否已经满足。
    fn is_satisfied(&self, guard: &SpinIrqGuard<'_, &mut Self::State>) -> bool;
}

#[repr(C)]
pub struct WaitQueue {
    inner: Spinlock<WaitQueueInner>,
}

#[repr(C)]
struct WaitQueueInner {
    waiters: ListHead<field_of!(Waiter, node)>,
    initialized: bool,
}

impl WaitQueue {
    pub const fn new_uninit() -> Self {
        Self {
            inner: Spinlock::new(WaitQueueInner::new()),
        }
    }

    pub fn init(&self) {
        unsafe { self.inner.init_with(|inner| inner.init()) };
    }

    /// 分配并初始化一个地址稳定的等待队列。
    pub fn try_new() -> Result<Pin<Box<Self, Kmalloc>>, MemoryError> {
        let queue = Box::try_new_in(Self::new_uninit(), Kmalloc::default())
            .map_err(|_| MemoryError::OutOfMemory)?;

        // SAFETY: queue 已经固定在 Box 中，初始化后不会再移动；当前尚未发布，
        // 不存在并发访问。
        unsafe {
            queue.inner.init_with(|inner| inner.init());
        }

        let queue = Box::into_pin(queue);

        Ok(queue)
    }

    /// 等待 condition 成立，并在返回时持有保护 condition 的锁。
    ///
    /// 唤醒只表示 condition 可能已经改变；若条件仍不成立，本函数会重新
    /// 进入等待流程。
    pub fn wait<'a, C>(&self, condition: &'a C) -> SpinIrqGuard<'a, &'a mut C::State>
    where
        C: WaitCondition + ?Sized,
    {
        assert!(
            interrupt::in_thread(),
            "WaitQueue::wait outside thread context"
        );

        loop {
            let condition_guard = condition.condition_lock().lock_irqsave();
            if condition.is_satisfied(&condition_guard) {
                return condition_guard;
            }

            let (condition_guard, _interrupt) = condition_guard.downgrade();

            let preempt = PreemptGuard::new();
            assert!(
                preempt.can_switch(),
                "WaitQueue::wait while preemption is disabled"
            );

            let scheduler = scheduler(&preempt);
            let current = scheduler.get_current();
            assert!(!scheduler.is_idle(&current), "idle thread must not wait");

            {
                let mut waiters = self.waiters().lock_pinned();

                // downgrade 后 condition_guard 只负责解锁；_interrupt 继续保持
                // 本地中断关闭，直到本次等待提交或早期唤醒处理完成。
                drop(condition_guard);

                assert!(
                    !current.waiter().is_linked(),
                    "thread is already in a WaitQueue"
                );
                current
                    .transition_to(ThreadState::Blocking)
                    .expect("only a Running thread can begin waiting");
                waiters.as_mut().enqueue(current.waiter());
            }

            {
                let _waiters = self.waiters().lock_pinned();
                match current.state() {
                    ThreadState::Running => {
                        assert!(
                            !current.waiter().is_linked(),
                            "early-woken thread is still in WaitQueue"
                        );
                        continue;
                    }
                    ThreadState::Blocking => {
                        assert!(
                            current.waiter().is_linked(),
                            "Blocking thread is missing its Waiter"
                        );
                        current.transition_to(ThreadState::Blocked).expect(
                            "Blocking thread must transition to Blocked before context switch",
                        );
                    }
                    state => panic!("invalid current state while committing wait: {state:?}"),
                }
            }

            drop(scheduler);
            Scheduler::switch_blocked(preempt, current, _interrupt);
        }
    }

    /// 唤醒 FIFO 队首的一个 waiter。
    ///
    /// `preempt` 必须覆盖调用者希望延迟到外层锁释放之后的调度。
    pub fn wake_one(&self, preempt: &PreemptGuard) -> bool {
        let scheduler = scheduler(preempt);
        self.wake_one_on(&scheduler)
    }

    fn wake_one_on(&self, scheduler: &Scheduler) -> bool {
        let mut waiters = self.waiters().lock_irqsave_pinned();

        let Some(made_ready) = WaitQueueInner::wake_first(&mut waiters, scheduler) else {
            return false;
        };
        if made_ready {
            request_reschedule();
        }
        true
    }

    /// 唤醒当前队列中的全部 waiter，返回从队列移除的数量。
    ///
    /// `preempt` 必须覆盖调用者希望延迟到外层锁释放之后的调度。
    pub fn wake_all(&self, preempt: &PreemptGuard) -> usize {
        let scheduler = scheduler(preempt);
        self.wake_all_on(&scheduler)
    }

    fn wake_all_on(&self, scheduler: &Scheduler) -> usize {
        let mut waiters = self.waiters().lock_irqsave_pinned();
        let mut count = 0;
        let mut made_ready = false;

        while let Some(ready) = WaitQueueInner::wake_first(&mut waiters, scheduler) {
            count += 1;
            made_ready |= ready;
        }
        if made_ready {
            request_reschedule();
        }
        count
    }

    pub fn is_empty(&self) -> bool {
        self.waiters().lock_pinned().is_empty()
    }

    fn inner(&self) -> &Spinlock<WaitQueueInner> {
        // SAFETY: inner 固定内嵌于已经被 Pin 的 WaitQueue 中。
        &self.inner
    }

    /// 调用方已经通过 condition 的 SpinIrqGuard 或 InterruptGuard 关闭中断。
    fn waiters(&self) -> Pin<&Spinlock<WaitQueueInner>> {
        unsafe { Pin::new_unchecked(self.inner()) }
    }

    /// C 调用者已经通过 `disable_preempt()` 固定了当前 CPU。
    fn wake_one_c(&self) -> bool {
        assert!(
            !can_preempt(),
            "C wait_queue_wake_one requires preemption to be disabled"
        );
        // SAFETY: C wrapper 的调用者契约保证当前 CPU 在整个调用期间不会切换。
        let scheduler = unsafe { scheduler_unchecked() };
        self.wake_one_on(scheduler)
    }

    /// C 调用者已经通过 `disable_preempt()` 固定了当前 CPU。
    fn wake_all_c(&self) -> usize {
        assert!(
            !can_preempt(),
            "C wait_queue_wake_all requires preemption to be disabled"
        );
        // SAFETY: C wrapper 的调用者契约保证当前 CPU 在整个调用期间不会切换。
        let scheduler = unsafe { scheduler_unchecked() };
        self.wake_all_on(scheduler)
    }
}

type CTryCondition = extern "C" fn(*mut c_void) -> c_int;

/// C 语言中使用的等待条件。
struct CWaitCondition {
    lock: *const Spinlock<()>,
    try_condition: CTryCondition,
    context: *mut c_void,
}

impl WaitCondition for CWaitCondition {
    type State = ();

    fn condition_lock(&self) -> &Spinlock<Self::State> {
        unsafe { &*self.lock }
    }

    fn is_satisfied(&self, _guard: &SpinIrqGuard<'_, &mut Self::State>) -> bool {
        (self.try_condition)(self.context) != 0
    }
}

#[unsafe(export_name = "wait_queue_init")]
extern "C" fn create_c(queue: *mut WaitQueue) {
    let wq = unsafe { queue.as_mut() }.expect("null WaitQueue from C");
    *wq = WaitQueue::new_uninit();
    wq.init();
}

#[unsafe(export_name = "wait_queue_wait")]
extern "C" fn wait_c(
    queue: *mut WaitQueue,
    condition_lock: *mut Spinlock<()>,
    try_condition: Option<CTryCondition>,
    context: *mut c_void,
) {
    let wq = unsafe { queue.as_mut() }.expect("null WaitQueue from C");
    assert!(!condition_lock.is_null(), "null WaitQueue condition lock");

    let condition = CWaitCondition {
        lock: condition_lock,
        try_condition: try_condition.expect("null WaitQueue condition callback"),
        context,
    };

    let _ = wq.wait(&condition);
}

#[unsafe(export_name = "wait_queue_wake_one")]
extern "C" fn wake_one_c(queue: *mut WaitQueue) {
    unsafe { queue.as_mut() }
        .expect("null WaitQueue from C")
        .wake_one_c();
}

#[unsafe(export_name = "wait_queue_wake_all")]
extern "C" fn wake_all_c(queue: *mut WaitQueue) {
    unsafe { queue.as_mut() }
        .expect("null WaitQueue from C")
        .wake_all_c();
}

impl Drop for WaitQueue {
    fn drop(&mut self) {
        let inner = self.inner.get_relaxed();
        if inner.initialized {
            assert!(inner.is_empty(), "dropping a WaitQueue with active waiters");
        }
    }
}

impl WaitQueueInner {
    const fn new() -> Self {
        Self {
            waiters: ListHead::default(),
            initialized: false,
        }
    }

    fn init(&mut self) {
        self.initialized = true;
        unsafe { self.waiters.init() };
    }

    fn is_empty(&self) -> bool {
        assert!(self.initialized, "WaitQueue used before initialization");
        self.waiters.is_empty()
    }

    fn enqueue(self: Pin<&mut Self>, waiter: &Waiter) {
        assert!(self.initialized, "WaitQueue used before initialization");
        assert!(!waiter.is_linked(), "Waiter added to two queues");

        unsafe {
            let waiters = &mut self.get_unchecked_mut().waiters;
            waiters.add_tail_ref(waiter)
        };
    }

    fn first(&self) -> Option<NonNull<Waiter>> {
        assert!(self.initialized, "WaitQueue used before initialization");

        unsafe { self.waiters.iter() }.next()
    }

    fn remove(self: Pin<&mut Self>, waiter: &Waiter) {
        assert!(self.initialized, "WaitQueue used before initialization");
        assert!(waiter.is_linked(), "removing an unlinked Waiter");

        unsafe { self.get_unchecked_mut().waiters.delete_ref(waiter) };
    }

    /// 唤醒列表里第一个线程
    ///
    /// 返回该 waiter 是否被加入 ready queue。Blocking waiter 仍是 current，
    /// 只需取消阻塞，不得重复加入 ready queue。
    fn wake_first<'a>(
        waiters: &mut SpinIrqGuard<'a, Pin<&'a mut Self>>,
        scheduler: &Scheduler,
    ) -> Option<bool> {
        let waiter_ptr = waiters.first()?;
        let waiter = unsafe { waiter_ptr.as_ref() };
        let thread = unsafe { Thread::from_waiter(waiter_ptr).as_ref() };

        match thread.state() {
            ThreadState::Blocking => {
                waiters.as_mut().remove(waiter);
                thread
                    .transition_to(ThreadState::Running)
                    .expect("Blocking thread must return to Running");
                Some(false)
            }
            ThreadState::Blocked => {
                waiters.as_mut().remove(waiter);
                thread
                    .transition_to(ThreadState::Waking)
                    .expect("Blocked thread must transition to Waking");

                let made_ready =
                    !thread.on_cpu.load(Ordering::Acquire) && scheduler.try_enqueue_woken(thread);
                Some(made_ready)
            }
            state => panic!("Waiter belongs to a non-waiting thread: {state:?}"),
        }
    }
}
