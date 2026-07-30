use core::{
    pin::Pin,
    ptr::{self, null_mut},
    sync::atomic::{AtomicBool, AtomicPtr, AtomicU8, AtomicU16, Ordering},
};

use crate::{
    arch::ArchInterrupt,
    kernel::{
        interrupt::{self, Interrupt, PreemptPoint},
        thread::{
            THREAD_MANAGER, Thread, ThreadArc, ThreadState, core::ThreadError,
            scheduler::ready_queue::ReadyQueue,
        },
    },
    lib::rust::spinlock::Spinlock,
};

mod preempt_guard;
mod ready_queue;

pub use preempt_guard::PreemptGuard;

const TIME_SLICE_MS: u16 = 100;

static SCHEDULER: Scheduler = Scheduler::new();

pub fn scheduler() -> &'static Scheduler {
    &SCHEDULER
}

pub trait Schedule {
    /// 从 ready queue 取出下一个候选线程。
    ///
    /// 返回 None 只表示 ready queue 为空；调用方根据当前线程状态决定继续运行
    /// 还是切换到 idle。返回的线程仍处于 Ready，尚未提交为 Running。
    fn get_next(&self, guard: &mut PreemptGuard) -> Option<&'static Thread>;
}

pub struct Scheduler {
    /// 就绪队列，存放所有处于 Ready 状态的线程
    ready: Spinlock<ReadyQueue>,
    /// 当前正在运行的线程
    current: AtomicPtr<Thread>,
    /// 已经切离 CPU、等待新线程完成切换收尾的线程。
    ///
    /// 切换前写入，切换后的线程在自己的栈上取走。禁止抢占和关闭本地中断
    /// 保证同一 CPU 上同时最多只有一个尚未完成的交接。
    previous: AtomicPtr<Thread>,
    /// 空闲线程，永远不会被调度器抢占
    ///
    /// 当需要切换到 idle 线程时，该字段会被设置为空，避免出现多个引用
    idle: AtomicPtr<Thread>,
    /// 已经退出调度系统、等待在下一个线程栈上释放 manager 引用的线程。
    pending_exit: AtomicPtr<Thread>,
    /// 抢占计数，当计数为 0 时，当前线程可以被抢占
    preempt_count: AtomicU8,
    resched: AtomicBool,
    slice_ms: AtomicU16,
}

impl Scheduler {
    const fn new() -> Self {
        Self {
            ready: Spinlock::new(ReadyQueue::new()),
            current: AtomicPtr::new(null_mut()),
            previous: AtomicPtr::new(null_mut()),
            idle: AtomicPtr::new(null_mut()),
            pending_exit: AtomicPtr::new(null_mut()),
            preempt_count: AtomicU8::new(0),
            resched: AtomicBool::new(false),
            slice_ms: AtomicU16::new(TIME_SLICE_MS),
        }
    }

    fn ready_queue(&self) -> Pin<&Spinlock<ReadyQueue>> {
        unsafe { Pin::new_unchecked(&self.ready) }
    }

    pub(super) fn init(&self, current: &'static Thread, idle: &'static Thread) {
        self.current
            .store(current as *const Thread as *mut Thread, Ordering::Relaxed);

        self.idle
            .store(idle as *const Thread as *mut Thread, Ordering::Relaxed);

        self.preempt_count.store(1, Ordering::Relaxed);

        let mut ready = self.ready_queue().lock_irqsave_pinned();
        ready.as_mut().init();
    }

    pub(super) fn enqueue(&self, thread: &Thread) -> Result<(), ThreadError> {
        unsafe {
            self.ready_queue()
                .lock_irqsave_pinned()
                .as_mut()
                .enqueue(thread)
        }
    }

    /// 尝试取得一个 Waking 线程的入队权。
    ///
    /// 唤醒 CPU 和原运行 CPU 的切换收尾都可能到达这里；状态转换与链表插入
    /// 在同一个 ready queue 临界区完成，只有仍处于 Waking 的一方能够成功。
    pub(super) fn try_enqueue_woken(&self, thread: &Thread) -> bool {
        unsafe {
            self.ready_queue()
                .lock_irqsave_pinned()
                .as_mut()
                .enqueue_woken(thread)
        }
    }

    pub(super) fn request_resched(&self) {
        self.resched.store(true, Ordering::Relaxed);
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

    /// 处理调度器时钟中断
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

    /// 在需要重新调度时，尝试在当前线程的安全抢占点进行抢占。
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
    pub(super) fn exit_self(&self) -> ! {
        assert!(interrupt::in_thread(), "thread_exit outside thread context");

        let mut guard = PreemptGuard::new(self);
        assert!(
            guard.can_switch(),
            "thread_exit while preemption is disabled"
        );

        let current = guard.current();

        let next = self.get_next(&mut guard).unwrap_or(self.idle());
        next.transition_to(ThreadState::Running)
            .expect("next thread must transition to Running");

        // 从发布 Dead 开始一直关闭中断，直到在 next 的上下文中完成切换收尾。
        // finish 必须在 ready queue 锁外执行，因为唤醒 joiner 会重新获取该锁。
        let _interrupt = ArchInterrupt::save_and_disable();
        unsafe { current.finish() };

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
    ///
    /// 切换成功返回 Some(())，否则返回 None
    fn schedule(&self, guard: &mut PreemptGuard<'_>) -> Option<()> {
        self.resched.store(false, Ordering::Relaxed);

        let current = guard.current();
        let next = self.get_next(guard);

        let next = if self.is_idle(current) {
            match next {
                Some(next) => {
                    current
                        .transition_to(ThreadState::Idle)
                        .expect("running idle thread must transition back to Idle");
                    next
                }
                None => return None,
            }
        } else {
            self.enqueue(current)
                .expect("running thread must transition back to Ready");

            next.unwrap_or(self.idle())
        };

        next.transition_to(ThreadState::Running)
            .expect("next thread must transition to Running");

        let _interrupt = ArchInterrupt::save_and_disable();

        // SAFETY: 已关闭中断，两个线程都由调度器独占，
        // 且 PreemptGuard 会跨越架构上下文切换保持有效。
        let _ = unsafe { guard.switch_thread(next) };
        Some(())
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

        self.slice_ms.store(TIME_SLICE_MS, Ordering::Relaxed);

        let current = self.current();
        current.on_cpu.store(true, Ordering::Relaxed);

        let previous = self.previous.swap(null_mut(), Ordering::Relaxed);
        if let Some(previous) = unsafe { previous.as_ref() } {
            previous.on_cpu.store(false, Ordering::Release);
            if self.try_enqueue_woken(previous) {
                self.request_resched();
            }
        }

        let exited = self.pending_exit.swap(null_mut(), Ordering::Relaxed);
        let exited = unsafe { exited.as_ref() }?;

        assert_eq!(exited.state(), ThreadState::Dead);

        Some(THREAD_MANAGER.remove(exited))
    }

    /// 在关闭本地中断且禁止抢占的切换窗口中发布 CPU 所有权交接。
    fn prepare_switch(&self, current: &'static Thread, next: &'static Thread) {
        debug_assert!(ptr::eq(self.current(), current));
        debug_assert!(!ptr::eq(current, next));

        self.previous
            .compare_exchange(
                null_mut(),
                current as *const Thread as *mut Thread,
                Ordering::Relaxed,
                Ordering::Relaxed,
            )
            .expect("previous context switch has not been finished");

        // next 已从 ready queue 移除并由本 CPU 独占。先声明 CPU 所有权，
        // 再发布 current，随后才真正恢复它的上下文。
        next.on_cpu.store(true, Ordering::Relaxed);
        self.current
            .store(next as *const Thread as *mut Thread, Ordering::Relaxed);
    }

    pub(super) fn current(&self) -> &'static Thread {
        // SAFETY: init 在调度开始前保存由 ThreadManager 持有的线程；
        // 后续写入的线程都具有相同的生命周期保证。
        unsafe {
            self.current
                .load(Ordering::Relaxed)
                .as_ref()
                .expect("scheduler is not initialized")
        }
    }

    pub(super) fn is_idle(&self, thread: &Thread) -> bool {
        ptr::eq(self.idle(), thread)
    }

    pub(super) fn idle(&self) -> &'static Thread {
        // SAFETY: idle 在线程管理器初始化期间注册，并且永远不会退出。
        unsafe {
            self.idle
                .load(Ordering::Relaxed)
                .as_ref()
                .expect("scheduler idle thread is not initialized")
        }
    }
}

impl Schedule for Scheduler {
    fn get_next(&self, guard: &mut PreemptGuard) -> Option<&'static Thread> {
        let current = guard.current();

        let mut queue = self.ready_queue().lock_irqsave_pinned();

        let next = if let Some(next) = queue.as_ref().next() {
            // SAFETY: 所有就绪线程都由 ThreadManager 持有；PreemptGuard 保证
            // 此处使用期间队列项不会被移除。
            let next: &'static Thread = unsafe { &*next.as_ptr() };

            debug_assert_ne!(
                current as *const Thread, next as *const Thread,
                "current thread must not be in the ready queue"
            );

            next
        } else {
            return None;
        };

        unsafe { queue.as_mut().dequeue(next) };

        Some(next)
    }
}
