use core::{
    cell::Cell,
    pin::Pin,
    ptr::{self, NonNull, null_mut},
    sync::atomic::{AtomicU16, Ordering},
};

use crate::{
    arch::{ArchCpuLocal, ArchInterrupt},
    cpu_local,
    kernel::{
        interrupt::{Interrupt, InterruptGuard},
        memory::{
            kmalloc::Kmalloc,
            percpu::{CpuLocal, CpuLocalGuard, PerCpuInit, PerCpuReadWrite},
        },
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
pub(in crate::kernel::thread) use preempt_guard::{can_preempt, disable_preempt, enable_preempt};

const TIME_SLICE_MS: u16 = 100;

cpu_local! {
    static SCHEDULER: Scheduler = Scheduler::new();
    /// 调度器是否需要重新调度
    ///
    /// `bool` 的操作数类型没有保证，因此使用 `u8` 代替
    static RESCHED: u8 = 0;
}

pub fn scheduler<'a>(guard: &'a PreemptGuard) -> CpuLocalGuard<'a, Scheduler> {
    SCHEDULER.get_local(guard)
}

/// 在调用方已经证明当前 CPU 不会发生抢占或上下文切换时获取调度器。
///
/// # Safety
///
/// 返回的引用整个使用期间，当前 CPU 必须保持固定，且不得发生上下文切换。
/// C wait-queue bridge 通过外部 `disable_preempt()` 满足这一条件。
pub(in crate::kernel::thread) unsafe fn scheduler_unchecked() -> &'static Scheduler {
    unsafe { &*ArchCpuLocal::get_ptr(&SCHEDULER) }
}

pub trait Schedule {
    /// 从 ready queue 取出下一个候选线程。
    ///
    /// 返回 None 只表示 ready queue 为空；调用方根据当前线程状态决定继续运行
    /// 还是切换到 idle。返回的线程仍处于 Ready，尚未提交为 Running。
    fn get_next(&self) -> Option<&'static Thread>;
}

pub struct Scheduler {
    /// 就绪队列，存放所有处于 Ready 状态的线程
    ready: Spinlock<ReadyQueue>,
    /// 当前正在运行的线程
    current: Cell<*mut Thread>,
    /// 已经切离 CPU、等待新线程完成切换收尾的线程
    ///
    /// 切换前写入，切换后的线程在自己的栈上取走。禁止抢占和关闭本地中断
    /// 保证同一 CPU 上同时最多只有一个尚未完成的交接
    previous: Cell<*mut Thread>,
    /// 空闲线程，永远不会被调度器抢占
    ///
    /// 当需要切换到 idle 线程时，该字段会被设置为空，避免出现多个引用
    idle: Cell<*mut Thread>,
    /// 已经退出调度系统、等待在下一个线程栈上释放 manager 引用的线程
    pending_exit: Cell<*mut Thread>,
    slice_ms: AtomicU16,
}

unsafe impl PerCpuInit for Scheduler {}

impl Scheduler {
    const fn new() -> Self {
        Self {
            ready: Spinlock::new(ReadyQueue::new()),
            current: Cell::new(null_mut()),
            previous: Cell::new(null_mut()),
            idle: Cell::new(null_mut()),
            pending_exit: Cell::new(null_mut()),
            slice_ms: AtomicU16::new(TIME_SLICE_MS),
        }
    }

    pub(super) fn start_first(guard: PreemptGuard, thread: &'static Thread) -> ! {
        assert!(
            guard.can_switch(),
            "initial thread requires switch capability"
        );
        guard.into_raw();
        Thread::prepare_first_thread(thread)
    }

    pub(super) fn finish_first_switch() -> (PreemptGuard, Option<ThreadArc>) {
        // SAFETY: start_first 和上下文切换路径均在恢复目标栈前留下唯一的 raw 状态
        let guard = unsafe { PreemptGuard::from_raw() };
        let exited = scheduler(&guard).finish_switch();
        (guard, exited)
    }

    /// 当前线程已经提交为 Blocked，选择目标并完成切换
    pub(super) fn switch_blocked(
        guard: PreemptGuard,
        current: ThreadArc,
        interrupt: InterruptGuard<'_, ArchInterrupt>,
    ) {
        let next = {
            let scheduler = scheduler(&guard);

            let next = scheduler.get_next().unwrap_or(scheduler.idle());
            next.transition_to(ThreadState::Running)
                .expect("next thread must transition to Running");

            scheduler.prepare_switch(next);
            next
        };

        guard.into_raw();

        unsafe { Thread::switch_context(current.as_ref(), next) };

        // SAFETY: 恢复当前栈的调度路径在切换前留下了唯一的 raw 状态
        let guard = unsafe { PreemptGuard::from_raw() };
        let _exited = scheduler(&guard).finish_switch();

        drop(guard);
        drop(interrupt);
        // _exited 需要最后释放
    }

    /// 执行一次普通线程调度；成功切换返回 Some，否则返回 None
    fn schedule(guard: PreemptGuard) -> Option<()> {
        RESCHED.write(0);

        let (current, next, interrupt) = {
            let scheduler = scheduler(&guard);
            let current = scheduler.get_current();
            let next = scheduler.get_next().unwrap_or(scheduler.idle());

            if scheduler.is_idle(current.as_ref()) {
                if scheduler.is_idle(next) {
                    return None;
                } else {
                    current
                        .transition_to(ThreadState::Idle)
                        .expect("idle thread must transition to Idle");
                }
            } else {
                scheduler
                    .enqueue(current.as_ref())
                    .expect("running thread must transition back to Ready");
            }

            next.transition_to(ThreadState::Running)
                .expect("next thread must transition to Running");

            let interrupt = ArchInterrupt::save_and_disable();

            scheduler.prepare_switch(next);

            (current, next, interrupt)
        };

        guard.into_raw();

        unsafe { Thread::switch_context(current.as_ref(), next) };

        // SAFETY: 恢复当前栈的调度路径在切换前留下了唯一的 raw 状态
        let guard = unsafe { PreemptGuard::from_raw() };
        let _exited = scheduler(&guard).finish_switch();

        drop(guard);
        drop(interrupt);

        // _exited 需要最后释放
        Some(())
    }

    /// 退出当前线程并永久切换到下一个线程
    fn exit_current(guard: PreemptGuard) -> ! {
        let _interrupt = ArchInterrupt::save_and_disable();

        let (current, next) = {
            let scheduler = scheduler(&guard);
            let current = NonNull::from(scheduler.current());

            // finish 必须在 ready queue 锁外执行，因为唤醒 joiner 会重新获取该锁
            unsafe { current.as_ref().finish(&guard) };

            // finish 中的唤醒已经被本次调度消费，随后选取目标时能够看到新入队的 joiner
            RESCHED.write(0);

            let next = scheduler.get_next().unwrap_or(scheduler.idle());
            next.transition_to(ThreadState::Running)
                .expect("next thread must transition to Running");

            let old = scheduler.pending_exit.replace(current.as_ptr());
            assert!(old.is_null(), "pending_exit must be empty before exit_self");

            scheduler.prepare_switch(next);

            (current, next)
        };

        guard.into_raw();

        unsafe { Thread::switch_context(current.as_ref(), next) };

        panic!("Dead thread resumed after context switch");
    }

    fn ready_queue(&self) -> Pin<&Spinlock<ReadyQueue>> {
        unsafe { Pin::new_unchecked(&self.ready) }
    }

    pub(super) fn init(&self, current: &'static Thread, idle: &'static Thread) {
        self.current.set(current as *const Thread as *mut Thread);

        self.idle.set(idle as *const Thread as *mut Thread);

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

    /// 尝试取得一个 Waking 线程的入队权
    ///
    /// 唤醒 CPU 和原运行 CPU 的切换收尾都可能到达这里；状态转换与链表插入
    /// 在同一个 ready queue 临界区完成，只有仍处于 Waking 的一方能够成功
    pub(super) fn try_enqueue_woken(&self, thread: &Thread) -> bool {
        unsafe {
            self.ready_queue()
                .lock_irqsave_pinned()
                .as_mut()
                .enqueue_woken(thread)
        }
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
            RESCHED.write(1);
        }
    }

    /// 在下一个线程的栈上移除已经退出线程的 manager 引用
    ///
    /// `pending_exit` 必须在 PreemptGuard 释放前处理，确保单槽状态不会在
    /// 清理完成前被下一次上下文切换覆盖
    fn finish_switch(&self) -> Option<ThreadArc> {
        self.slice_ms.store(TIME_SLICE_MS, Ordering::Relaxed);

        let current = self.current();
        current.on_cpu.store(true, Ordering::Relaxed);

        let previous = self.previous.take();
        if let Some(previous) = unsafe { previous.as_ref() } {
            previous.on_cpu.store(false, Ordering::Release);
            if self.try_enqueue_woken(previous) {
                RESCHED.write(1);
            }
        }

        let exited = self.pending_exit.take();
        let exited = unsafe { exited.as_ref() }?;

        assert_eq!(exited.state(), ThreadState::Dead);

        Some(THREAD_MANAGER.remove(exited))
    }

    /// 在关闭本地中断且禁止抢占的切换窗口中发布 CPU 所有权交接
    fn prepare_switch(&self, next: &Thread) {
        let current = self.current.get();
        debug_assert!(!ptr::eq(current, next));

        let old = self
            .previous
            .replace(current as *const Thread as *mut Thread);
        assert!(
            old.is_null(),
            "previous context switch has not been finished"
        );

        // next 已从 ready queue 移除并由本 CPU 独占。先声明 CPU 所有权，
        // 再发布 current，随后才真正恢复它的上下文
        next.on_cpu.store(true, Ordering::Relaxed);
        self.current.set(next as *const Thread as *mut Thread);
    }

    /// 获取当前正在运行的线程
    ///
    /// 由于返回的引用生命周期依赖于 `self`，长时间持有 `CpuLocalGuard`
    /// 可能会阻塞其他线程的调度。请尽量使用 get_current 获取 owning handle
    pub(super) fn current(&self) -> &Thread {
        // SAFETY: init 在调度开始前保存由 ThreadManager 持有的线程；
        // 后续写入的线程都具有相同的生命周期保证。
        unsafe { &*self.current.get() }
    }

    /// 获取当前正在运行的线程的 owning handle
    pub(super) fn get_current(&self) -> ThreadArc {
        let current = self.current.get();

        // SAFETY: current 始终由 ThreadManager 持有；先增加强引用计数，再把新增
        // 的计数恢复成 owning handle
        unsafe {
            ThreadArc::increment_strong_count_in(current, Kmalloc::default());
            ThreadArc::from_raw_in(current, Kmalloc::default())
        }
    }

    pub(super) fn is_idle(&self, thread: &Thread) -> bool {
        ptr::eq(self.idle(), thread)
    }

    pub(super) fn idle(&self) -> &'static Thread {
        // SAFETY: idle 在线程管理器初始化期间注册，并且永远不会退出。
        unsafe {
            self.idle
                .get()
                .as_ref()
                .expect("scheduler idle thread is not initialized")
        }
    }
}

impl Schedule for Scheduler {
    fn get_next(&self) -> Option<&'static Thread> {
        let current = self.current();

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

pub fn request_reschedule() {
    RESCHED.write(1);
}
