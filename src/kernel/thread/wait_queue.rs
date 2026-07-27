use alloc::boxed::Box;
use core::{
    cell::SyncUnsafeCell,
    ffi::{c_int, c_void},
    marker::PhantomPinned,
    mem::offset_of,
    pin::Pin,
    ptr::{self, NonNull},
};

use crate::{
    arch::ArchInterrupt,
    kernel::{
        interrupt::{self, Interrupt},
        memory::{MemoryError, kmalloc::Kmalloc},
        thread::{
            Thread, ThreadState,
            scheduler::{PreemptGuard, scheduler},
        },
    },
    lib::rust::{
        list::{ListHead, ListNode},
        spinlock::{SpinGuard, SpinIrqGuard, Spinlock},
    },
};

/// 一次线程等待所使用的侵入式队列成员。
///
/// Waiter 固定内嵌于 Thread，但等待队列只管理 Waiter，不直接管理 Thread
/// 的运行队列节点。当前阶段一条线程同时只能进入一个等待队列。
pub struct Waiter {
    node: SyncUnsafeCell<ListNode<Waiter>>,
}

impl Waiter {
    pub(super) const fn new() -> Self {
        Self {
            node: SyncUnsafeCell::new(ListNode::new()),
        }
    }

    /// 获取等待队列节点。调用方必须持有对应 WaitQueue 的锁。
    ///
    /// # Safety
    ///
    /// Waiter 必须内嵌于仍然存活且地址稳定的 Thread 中。
    pub(super) unsafe fn node(&self) -> &mut ListNode<Waiter> {
        unsafe { &mut *self.node.get() }
    }

    pub(super) const fn node_offset() -> usize {
        offset_of!(Waiter, node)
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
    _pin: PhantomPinned,
}

#[repr(C)]
struct WaitQueueInner {
    waiters: ListHead<Waiter>,
    initialized: bool,
}

impl WaitQueue {
    pub const fn new() -> Self {
        Self {
            inner: Spinlock::new(WaitQueueInner::new()),
            _pin: PhantomPinned,
        }
    }

    pub fn init(&self) {
        unsafe { self.inner.init_with(|inner| inner.init()) };
    }

    /// 分配并初始化一个地址稳定的等待队列。
    pub fn try_new() -> Result<Pin<Box<Self, Kmalloc>>, MemoryError> {
        let queue = Box::try_new_in(
            Self {
                inner: Spinlock::new(WaitQueueInner::new()),
                _pin: PhantomPinned,
            },
            Kmalloc::default(),
        )
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

            let scheduler = scheduler();
            let mut preempt = PreemptGuard::new(&scheduler);
            assert!(
                preempt.can_switch(),
                "WaitQueue::wait while preemption is disabled"
            );

            let current = preempt.current();
            assert!(!scheduler.is_idle(current), "idle thread must not wait");

            {
                let mut waiters = self.waiters();

                assert!(
                    !current.waiter().is_linked(),
                    "thread is already in a WaitQueue"
                );
                current
                    .transition_to(ThreadState::Blocking)
                    .expect("only a Running thread can begin waiting");
                unsafe { waiters.as_mut().get_unchecked_mut() }.enqueue(current.waiter());
            }

            // condition_guard 的 Drop 完整执行 unlock + irqrestore。此后中断可以
            // 唤醒仍处于 Blocking 的 current，但 PreemptGuard 保证它不会被切走。
            drop(condition_guard);

            let interrupt = ArchInterrupt::save_and_disable();
            let next = {
                let _waiters = self.waiters();

                match current.state() {
                    ThreadState::Running => {
                        assert!(
                            !current.waiter().is_linked(),
                            "early-woken thread is still in WaitQueue"
                        );
                        None
                    }
                    ThreadState::Blocking => {
                        assert!(
                            current.waiter().is_linked(),
                            "Blocking thread is missing its Waiter"
                        );
                        Some(unsafe { scheduler.commit_block() })
                    }
                    state => panic!("invalid current state while committing wait: {state:?}"),
                }
            };

            let Some(next) = next else {
                continue;
            };

            // SAFETY: IRQ 已关闭，current 已经提交为 Blocked，next 已经成为
            // Running，且 PreemptGuard 跨越上下文切换保持有效。
            let _exited = unsafe { preempt.switch_thread(next) };

            // 先恢复中断再释放 _exited，避免阻塞中断处理
            drop(interrupt);
        }
    }

    /// 唤醒 FIFO 队首的一个 waiter。
    pub fn wake_one(&self) -> bool {
        let mut waiters = self.waiters_irqsave();

        let Some(made_ready) = (unsafe { waiters.as_mut().get_unchecked_mut().wake_first() })
        else {
            return false;
        };
        if made_ready {
            scheduler().request_resched();
        }
        true
    }

    /// 唤醒当前队列中的全部 waiter，返回从队列移除的数量。
    pub fn wake_all(&self) -> usize {
        let mut waiters = self.waiters_irqsave();
        let mut count = 0;
        let mut made_ready = false;

        while let Some(ready) = unsafe { waiters.as_mut().get_unchecked_mut().wake_first() } {
            count += 1;
            made_ready |= ready;
        }
        if made_ready {
            scheduler().request_resched();
        }
        count
    }

    pub fn is_empty(&self) -> bool {
        self.waiters_irqsave().is_empty()
    }

    fn inner(&self) -> &Spinlock<WaitQueueInner> {
        // SAFETY: inner 固定内嵌于已经被 Pin 的 WaitQueue 中。
        &self.inner
    }

    /// 调用方已经通过 condition 的 SpinIrqGuard 或 InterruptGuard 关闭中断。
    fn waiters(&self) -> SpinGuard<'_, Pin<&mut WaitQueueInner>> {
        unsafe { Pin::new_unchecked(self.inner()).lock_pinned() }
    }

    fn waiters_irqsave(&self) -> SpinIrqGuard<'_, Pin<&mut WaitQueueInner>> {
        unsafe { Pin::new_unchecked(self.inner()).lock_irqsave_pinned() }
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
    *wq = WaitQueue::new();
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
        .wake_one();
}

#[unsafe(export_name = "wait_queue_wake_all")]
extern "C" fn wake_all_c(queue: *mut WaitQueue) {
    unsafe { queue.as_mut() }
        .expect("null WaitQueue from C")
        .wake_all();
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
            waiters: ListHead::empty(),
            initialized: false,
        }
    }

    fn init(&mut self) {
        self.initialized = true;
        self.waiters.init();
    }

    fn is_empty(&self) -> bool {
        assert!(self.initialized, "WaitQueue used before initialization");
        self.waiters.is_empty()
    }

    fn enqueue(&mut self, waiter: &Waiter) {
        assert!(self.initialized, "WaitQueue used before initialization");
        assert!(!waiter.is_linked(), "Waiter added to two queues");

        let waiters = &mut self.waiters;
        unsafe { waiters.add_tail(waiter.node()) };
    }

    fn first(&self) -> Option<NonNull<Waiter>> {
        assert!(self.initialized, "WaitQueue used before initialization");

        self.waiters.iter(Waiter::node_offset()).next()
    }

    fn remove(&mut self, waiter: &Waiter) {
        assert!(self.initialized, "WaitQueue used before initialization");
        assert!(waiter.is_linked(), "removing an unlinked Waiter");

        unsafe { self.waiters.delete(waiter.node()) };
    }

    /// 返回该 waiter 是否被加入 ready queue。Blocking waiter 仍是 current，
    /// 只需取消阻塞，不得重复加入 ready queue。
    fn wake_first(&mut self) -> Option<bool> {
        let waiter_ptr = self.first()?;
        let waiter = unsafe { waiter_ptr.as_ref() };
        let thread = unsafe { Thread::from_waiter(waiter_ptr).as_ref() };

        match thread.state() {
            ThreadState::Blocking => {
                assert!(
                    ptr::eq(scheduler().current(), thread),
                    "a non-current thread is in Blocking state"
                );
                self.remove(waiter);
                thread
                    .transition_to(ThreadState::Running)
                    .expect("Blocking thread must return to Running");
                Some(false)
            }
            ThreadState::Blocked => {
                self.remove(waiter);
                unsafe { scheduler().enqueue_woken(thread) };
                Some(true)
            }
            state => panic!("Waiter belongs to a non-waiting thread: {state:?}"),
        }
    }
}
