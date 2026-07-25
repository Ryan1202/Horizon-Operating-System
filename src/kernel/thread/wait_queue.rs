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
    pub(super) unsafe fn node(&self) -> Pin<&mut ListNode<Waiter>> {
        unsafe { Pin::new_unchecked(&mut *self.node.get()) }
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

pub struct WaitQueue {
    inner: Spinlock<WaitQueueInner>,
    _pin: PhantomPinned,
}

struct WaitQueueInner {
    waiters: ListHead<Waiter>,
    initialized: bool,
}

impl WaitQueue {
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
        let queue = Box::into_pin(queue);

        // SAFETY: queue 已经固定在 Box 中，初始化后不会再移动；当前尚未发布，
        // 不存在并发访问。
        unsafe {
            let inner = queue.as_ref().map_unchecked(|queue| &queue.inner);
            inner.init_with_pinned(|inner| inner.init());
        }

        Ok(queue)
    }

    /// 等待 condition 成立，并在返回时持有保护 condition 的锁。
    ///
    /// 唤醒只表示 condition 可能已经改变；若条件仍不成立，本函数会重新
    /// 进入等待流程。
    pub fn wait<'a, C>(self: Pin<&Self>, condition: &'a C) -> SpinIrqGuard<'a, &'a mut C::State>
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
                waiters.enqueue(current.waiter());
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
    pub fn wake_one(self: Pin<&Self>) -> bool {
        let mut waiters = self.waiters_irqsave();

        let Some(made_ready) = waiters.wake_first() else {
            return false;
        };
        if made_ready {
            scheduler().request_resched();
        }
        true
    }

    /// 唤醒当前队列中的全部 waiter，返回从队列移除的数量。
    pub fn wake_all(self: Pin<&Self>) -> usize {
        let mut waiters = self.waiters_irqsave();
        let mut count = 0;
        let mut made_ready = false;

        while let Some(ready) = waiters.wake_first() {
            count += 1;
            made_ready |= ready;
        }
        if made_ready {
            scheduler().request_resched();
        }
        count
    }

    pub fn is_empty(self: Pin<&Self>) -> bool {
        self.waiters_irqsave().is_empty()
    }

    fn inner(self: Pin<&Self>) -> Pin<&Spinlock<WaitQueueInner>> {
        // SAFETY: inner 固定内嵌于已经被 Pin 的 WaitQueue 中。
        unsafe { self.map_unchecked(|queue| &queue.inner) }
    }

    /// 调用方已经通过 condition 的 SpinIrqGuard 或 InterruptGuard 关闭中断。
    fn waiters(self: Pin<&Self>) -> SpinGuard<'_, Pin<&mut WaitQueueInner>> {
        self.inner().lock_pinned()
    }

    fn waiters_irqsave(self: Pin<&Self>) -> SpinIrqGuard<'_, Pin<&mut WaitQueueInner>> {
        self.inner().lock_irqsave_pinned()
    }
}

pub(super) fn into_raw(queue: Pin<Box<WaitQueue, Kmalloc>>) -> *mut WaitQueue {
    let queue = unsafe { Pin::into_inner_unchecked(queue) };
    Box::leak(queue)
}

/// 从 C 持有的队列头指针恢复固定地址引用。
///
/// # Safety
///
/// `queue` 必须来自 [`into_raw`]，并且尚未通过 [`drop_raw`] 释放。
pub(super) unsafe fn from_raw<'a>(queue: *mut WaitQueue) -> Pin<&'a WaitQueue> {
    assert!(!queue.is_null(), "null WaitQueue");
    unsafe { Pin::new_unchecked(&*queue) }
}

/// 释放由 C 持有的队列头。
///
/// # Safety
///
/// `queue` 必须来自 [`into_raw`]，且只能调用一次。调用方必须保证队列中
/// 已经没有 waiter。
pub(super) unsafe fn drop_raw(queue: *mut WaitQueue) {
    assert!(!queue.is_null(), "null WaitQueue");
    let _ = unsafe { Box::<WaitQueue, Kmalloc>::from_raw_in(queue, Kmalloc::default()) };
}

type CTryCondition = extern "C" fn(*mut c_void) -> c_int;

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

#[unsafe(export_name = "wait_queue_create")]
extern "C" fn create_c() -> *mut WaitQueue {
    WaitQueue::try_new().map_or(ptr::null_mut(), into_raw)
}

#[unsafe(export_name = "wait_queue_destroy")]
extern "C" fn destroy_c(queue: *mut WaitQueue) {
    if queue.is_null() {
        return;
    }
    unsafe { drop_raw(queue) };
}

#[unsafe(export_name = "wait_queue_wait")]
extern "C" fn wait_c(
    queue: *mut WaitQueue,
    condition_lock: *mut Spinlock<()>,
    try_condition: Option<CTryCondition>,
    context: *mut c_void,
) {
    assert!(!condition_lock.is_null(), "null WaitQueue condition lock");
    let condition = CWaitCondition {
        lock: condition_lock,
        try_condition: try_condition.expect("null WaitQueue condition callback"),
        context,
    };
    let _ = unsafe { from_raw(queue) }.wait(&condition);
}

#[unsafe(export_name = "wait_queue_wake_one")]
extern "C" fn wake_one_c(queue: *mut WaitQueue) {
    unsafe { from_raw(queue) }.wake_one();
}

#[unsafe(export_name = "wait_queue_wake_all")]
extern "C" fn wake_all_c(queue: *mut WaitQueue) {
    unsafe { from_raw(queue) }.wake_all();
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

    fn init(mut self: Pin<&mut Self>) {
        unsafe {
            self.as_mut()
                .map_unchecked_mut(|inner| {
                    inner.initialized = true;
                    &mut inner.waiters
                })
                .init()
        };
    }

    fn is_empty(&self) -> bool {
        assert!(self.initialized, "WaitQueue used before initialization");
        self.waiters.is_empty()
    }

    fn enqueue(self: &mut Pin<&mut Self>, waiter: &Waiter) {
        assert!(self.initialized, "WaitQueue used before initialization");
        assert!(!waiter.is_linked(), "Waiter added to two queues");

        let mut waiters = unsafe { self.as_mut().map_unchecked_mut(|inner| &mut inner.waiters) };
        unsafe { waiters.add_tail(waiter.node()) };
    }

    fn first(self: &Pin<&Self>) -> Option<NonNull<Waiter>> {
        assert!(self.initialized, "WaitQueue used before initialization");

        let waiters = unsafe { self.map_unchecked(|inner| &inner.waiters) };
        waiters.iter(Waiter::node_offset()).next()
    }

    fn remove(self: &mut Pin<&mut Self>, waiter: &Waiter) {
        assert!(self.initialized, "WaitQueue used before initialization");
        assert!(waiter.is_linked(), "removing an unlinked Waiter");

        let mut waiters = unsafe { self.as_mut().map_unchecked_mut(|inner| &mut inner.waiters) };
        unsafe { waiters.delete(waiter.node()) };
    }

    /// 返回该 waiter 是否被加入 ready queue。Blocking waiter 仍是 current，
    /// 只需取消阻塞，不得重复加入 ready queue。
    fn wake_first(self: &mut Pin<&mut Self>) -> Option<bool> {
        let waiter_ptr = self.as_ref().first()?;
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
