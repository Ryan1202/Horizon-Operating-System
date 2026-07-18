use core::{
    pin::Pin,
    ptr::{self, NonNull, null_mut},
    sync::atomic::{AtomicBool, AtomicPtr, AtomicU8, AtomicU16, Ordering},
};

use crate::{
    arch::ArchInterrupt,
    kernel::{
        interrupt::{self, Interrupt, PreemptPoint},
        thread::{THREAD_MANAGER, Thread, ThreadArc, ThreadState, core::ThreadError},
    },
    lib::rust::{list::ListHead, spinlock::Spinlock},
};

const TIME_SLICE_MS: u16 = 100;

pub static SCHEDULER: Scheduler = Scheduler::new();

pub struct Scheduler {
    /// 就绪队列，存放所有处于 Ready 状态的线程
    ready: Spinlock<ReadyQueue>,
    /// 当前正在运行的线程
    current: AtomicPtr<Thread>,
    /// 空闲线程，永远不会被调度器抢占
    ///
    /// 当需要切换到 idle 线程时，该字段会被设置为空，避免出现多个引用
    idle: AtomicPtr<Thread>,
    /// 已经退出调度系统、等待在下一个线程栈上释放 manager 引用的线程。
    pending_exit: AtomicPtr<Thread>,
    preempt_count: AtomicU8,
    resched: AtomicBool,
    slice_ms: AtomicU16,
}

struct ReadyQueue {
    head: ListHead<Thread>,
}

impl Scheduler {
    const fn new() -> Self {
        Self {
            ready: Spinlock::new(ReadyQueue::new()),
            current: AtomicPtr::new(null_mut()),
            idle: AtomicPtr::new(null_mut()),
            pending_exit: AtomicPtr::new(null_mut()),
            preempt_count: AtomicU8::new(0),
            resched: AtomicBool::new(false),
            slice_ms: AtomicU16::new(TIME_SLICE_MS),
        }
    }

    pub(super) fn init(&self, current: &'static Thread, idle: &'static Thread) {
        self.current
            .compare_exchange(
                null_mut(),
                current as *const Thread as *mut Thread,
                Ordering::Relaxed,
                Ordering::Relaxed,
            )
            .expect("scheduler initialized twice");
        self.idle
            .compare_exchange(
                null_mut(),
                idle as *const Thread as *mut Thread,
                Ordering::Relaxed,
                Ordering::Relaxed,
            )
            .expect("scheduler idle thread initialized twice");
        self.preempt_count.store(1, Ordering::Relaxed);
        self.ready.lock_irqsave().init();
    }

    pub(super) fn enqueue(&self, thread: &Thread) -> Result<(), ThreadError> {
        unsafe { self.ready.lock_irqsave().enqueue(thread) }
    }

    /// 手动禁止抢占，并返回调用前的抢占计数。
    ///
    /// # Safety
    ///
    /// 必须保证随后存在同一 CPU 上配对的 `enable_from_c`。
    pub(super) unsafe fn disable_preempt(&self) -> u8 {
        self.preempt_count
            .try_update(Ordering::Relaxed, Ordering::Relaxed, |count| {
                count.checked_add(1)
            })
            .expect("preempt count overflow")
    }

    /// 手动恢复抢占。
    ///
    /// # Safety
    ///
    /// 必须保证此前存在同一 CPU 上配对的 `disable_from_c`。
    pub(super) unsafe fn enable_preempt(&self) {
        let previous = self
            .preempt_count
            .try_update(Ordering::Relaxed, Ordering::Relaxed, |count| {
                count.checked_sub(1)
            })
            .expect("unbalanced enable_preempt");

        // 如果之前的计数为 1，说明当前线程已经可以被抢占了，并且有调度请求挂起，那么就尝试进行抢占。
        if previous == 1 && self.resched.load(Ordering::Relaxed) {
            if let Some(point) = PreemptPoint::new() {
                point.try_preempt();
            }
        }
    }

    /// 完成新线程首次上下文切换时遗留的退出清理和抢占计数配平。
    ///
    /// # Safety
    ///
    /// 必须保证当前线程是首次上下文切换的目标线程，并且在调用期间禁止抢占。
    pub(super) unsafe fn finish_first_switch(&self) {
        let mut guard = unsafe { PreemptGuard::from_first_switch(self) };
        let _ = self.finish_switch(&mut guard);

        ArchInterrupt::enable();
    }

    /// 检查当前线程是否可以被抢占。
    pub(super) fn can_preempt(&self) -> bool {
        self.preempt_count.load(Ordering::Relaxed) == 0
    }

    /// 处理调度器时钟中断。
    pub(super) fn tick(&self, elapsed_ms: u16) {
        if elapsed_ms == 0 {
            return;
        }

        let previous = self
            .slice_ms
            .update(Ordering::Relaxed, Ordering::Relaxed, |remaining| {
                if remaining <= elapsed_ms {
                    TIME_SLICE_MS
                } else {
                    remaining - elapsed_ms
                }
            });

        if previous <= elapsed_ms {
            self.resched.store(true, Ordering::Relaxed);
        }
    }

    /// 尝试在当前线程的安全抢占点进行抢占。
    pub fn try_preempt(&self, _point: PreemptPoint) {
        if !self.resched.load(Ordering::Relaxed) {
            return;
        }

        let mut guard = PreemptGuard::new(self);
        if !guard.can_switch() {
            return;
        }

        self.schedule(&mut guard);
    }

    /// 主动让出当前 CPU，可能失败
    pub(super) fn try_yield(&self, _point: PreemptPoint) {
        let mut guard = PreemptGuard::new(self);
        if !guard.can_switch() {
            return;
        }

        self.schedule(&mut guard);
    }

    /// 退出当前线程。线程进入 Dead 后不会再次成为调度候选。
    pub(super) fn exit_current(&self) -> ! {
        assert!(interrupt::in_thread(), "thread_exit outside thread context");

        let guard = PreemptGuard::new(self);
        assert!(
            guard.can_switch(),
            "thread_exit while preemption is disabled"
        );

        let current = guard.current();
        assert!(!self.is_idle(current), "idle thread must not exit");

        let next = {
            let mut ready_queue = self.ready.lock_irqsave();

            current
                .transition_to(ThreadState::Dead)
                .expect("running thread must transition to Dead");

            if let Some(next) = ready_queue.next() {
                // SAFETY: Ready 线程由 ThreadManager 持有，并且 ready queue
                // 的锁保证节点在本次访问期间保持有效。
                let next: &'static Thread = unsafe { &*next.as_ptr() };
                unsafe { ready_queue.dequeue(next, ThreadState::Running) }
                    .expect("ready thread must transition to Running");
                next
            } else {
                let idle = self.idle();
                idle.transition_to(ThreadState::Running)
                    .expect("idle thread must transition to Running");
                idle
            }
        };

        let _interrupt = ArchInterrupt::save_and_disable();

        self.pending_exit
            .compare_exchange(
                null_mut(),
                current as *const Thread as *mut Thread,
                Ordering::Relaxed,
                Ordering::Relaxed,
            )
            .expect("previous exited thread has not been handled");

        self.resched.store(false, Ordering::Relaxed);

        // SAFETY: InterruptGuard 关闭了中断；guard 证明当前 CPU 已禁止抢占；
        // next 已经转换为 Running，current 已经转换为 Dead。
        unsafe { guard.exit_to(next) }
    }

    /// 执行线程切换
    fn schedule(&self, guard: &mut PreemptGuard<'_>) {
        self.resched.store(false, Ordering::Relaxed);

        let current = guard.current();

        let next = {
            let mut ready_queue = self.ready.lock_irqsave();
            let Some(next) = ready_queue.next() else {
                // 如果没有就绪线程，则继续运行当前线程，并积极尝试在下一次抢占点进行调度。
                self.resched.store(true, Ordering::Relaxed);
                return;
            };

            // SAFETY: 所有就绪线程都由 ThreadManager 持有；PreemptGuard 保证
            // 此处使用期间队列项不会被移除。
            let next: &'static Thread = unsafe { &*next.as_ptr() };

            debug_assert_ne!(
                current as *const Thread, next as *const Thread,
                "current thread must not be in the ready queue"
            );

            if self.is_idle(current) {
                current
                    .transition_to(ThreadState::Idle)
                    .expect("running idle thread must transition back to Idle");
            } else {
                unsafe { ready_queue.enqueue(current) }
                    .expect("running thread must transition back to Ready");
            }

            unsafe { ready_queue.dequeue(next, ThreadState::Running) }
                .expect("ready thread must transition to Running");

            next
        };

        let _interrupt = ArchInterrupt::save_and_disable();
        // SAFETY: 已关闭中断，两个线程都由调度器独占，
        // 且 PreemptGuard 会跨越架构上下文切换保持有效。
        let _ = unsafe { guard.switch_thread(next) };
    }

    /// 在下一个线程的栈上移除已经退出线程的 manager 引用。
    ///
    /// `pending_exit` 必须在 PreemptGuard 释放前处理，确保单槽状态不会在
    /// 清理完成前被下一次上下文切换覆盖。
    fn finish_switch(&self, guard: &mut PreemptGuard<'_>) -> Option<ThreadArc> {
        assert!(
            ptr::eq(guard.scheduler, self),
            "PreemptGuard belongs to another scheduler"
        );
        assert!(
            guard.can_switch(),
            "pending exit handled without switch capability"
        );

        let exited = self.pending_exit.swap(null_mut(), Ordering::Relaxed);
        let exited = unsafe { exited.as_ref() }?;

        assert_eq!(exited.state(), ThreadState::Dead);

        Some(THREAD_MANAGER.remove(exited))
    }

    fn current(&self) -> &'static Thread {
        // SAFETY: init 在调度开始前保存由 ThreadManager 持有的线程；
        // 后续写入的线程都具有相同的生命周期保证。
        unsafe {
            self.current
                .load(Ordering::Relaxed)
                .as_ref()
                .expect("scheduler is not initialized")
        }
    }

    fn is_idle(&self, thread: &Thread) -> bool {
        ptr::eq(self.idle(), thread)
    }

    fn idle(&self) -> &'static Thread {
        // SAFETY: idle 在线程管理器初始化期间注册，并且永远不会退出。
        unsafe {
            self.idle
                .load(Ordering::Relaxed)
                .as_ref()
                .expect("scheduler idle thread is not initialized")
        }
    }
}

#[must_use = "the scheduler preemption count must be restored"]
struct PreemptGuard<'a> {
    scheduler: &'a Scheduler,
    can_switch: bool,
}

impl<'a> PreemptGuard<'a> {
    fn new(scheduler: &'a Scheduler) -> Self {
        Self {
            scheduler,
            can_switch: unsafe { scheduler.disable_preempt() } == 0,
        }
    }

    /// 接管首次上下文切换时由前一个线程传递过来的抢占计数。
    ///
    /// # Safety
    ///
    /// 当前 CPU 的抢占计数必须恰好包含一次尚未配平的上下文切换计数。
    unsafe fn from_first_switch(scheduler: &'a Scheduler) -> Self {
        assert_eq!(
            scheduler.preempt_count.load(Ordering::Relaxed),
            1,
            "invalid preempt count on first switch"
        );
        Self {
            scheduler,
            can_switch: true,
        }
    }

    /// 检查当前是否可以切换上下文
    fn can_switch(&self) -> bool {
        self.can_switch
    }

    /// 获取当前正在运行的线程
    fn current(&self) -> &'static Thread {
        self.scheduler.current()
    }

    /// 切换到指定的线程上下文
    ///
    /// # Safety
    ///
    /// `next` 必须由调度器独占，并且已经从就绪队列转换为 Running，
    /// 并且需要关闭中断确保中间不会发生其他线程切换以及在中间状态被打断
    unsafe fn switch_thread(&mut self, next: &'static Thread) -> Option<ThreadArc> {
        let current = self.current();

        self.scheduler
            .slice_ms
            .store(TIME_SLICE_MS, Ordering::Relaxed);
        self.scheduler
            .current
            .store(next as *const Thread as *mut Thread, Ordering::Relaxed);

        unsafe { Thread::switch_context(current, next) };
        self.scheduler.finish_switch(self)
    }

    /// 永久切离已经进入 Dead 状态的当前线程。
    ///
    /// # Safety
    ///
    /// 调用期间必须关闭中断，next 必须已经转换为 Running。
    unsafe fn exit_to(self, next: &'static Thread) -> ! {
        let current = self.current();

        self.scheduler
            .slice_ms
            .store(TIME_SLICE_MS, Ordering::Relaxed);
        self.scheduler
            .current
            .store(next as *const Thread as *mut Thread, Ordering::Relaxed);

        unsafe { Thread::switch_context(current, next) };
        panic!("Dead thread resumed after context switch")
    }
}

impl Drop for PreemptGuard<'_> {
    fn drop(&mut self) {
        unsafe { self.scheduler.enable_preempt() };
    }
}

impl ReadyQueue {
    const fn new() -> Self {
        Self {
            head: ListHead::empty(),
        }
    }

    fn init(&mut self) {
        unsafe { Pin::new_unchecked(&mut self.head).init() };
    }

    unsafe fn head(&mut self) -> Pin<&mut ListHead<Thread>> {
        unsafe { Pin::new_unchecked(&mut self.head) }
    }

    unsafe fn enqueue(&mut self, thread: &Thread) -> Result<(), ThreadError> {
        thread.transition_to(ThreadState::Ready)?;
        unsafe { self.head().add_tail(thread.get_node()) };
        Ok(())
    }

    unsafe fn dequeue(&mut self, thread: &Thread, state: ThreadState) -> Result<(), ThreadError> {
        thread.transition_to(state)?;
        unsafe { self.head().delete(thread.get_node()) };
        Ok(())
    }

    fn next(&mut self) -> Option<NonNull<Thread>> {
        unsafe { self.head() }.iter(Thread::list_offset()).next()
    }
}
