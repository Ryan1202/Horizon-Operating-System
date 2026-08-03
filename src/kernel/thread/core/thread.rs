use core::{
    cell::SyncUnsafeCell,
    ffi::{CStr, c_void},
    mem::offset_of,
    ptr::{self, NonNull},
    sync::atomic::AtomicBool,
};

use crate::{
    arch::ArchThreadContext,
    container_of,
    kernel::{
        interrupt::in_thread,
        memory::MemoryError,
        thread::{
            KernelThreadEntry, ThreadId, ThreadState, WaitCondition, WaitQueue,
            core::{KernelStack, ThreadContext, ThreadError},
            scheduler::{PreemptGuard, can_preempt, scheduler},
            wait_queue::Waiter,
        },
    },
    lib::rust::{
        list::ListNode,
        spinlock::{SpinIrqGuard, Spinlock},
    },
};

pub struct Thread {
    id: ThreadId,
    name: &'static CStr,

    run_node: SyncUnsafeCell<ListNode<Thread>>,
    thread_node: SyncUnsafeCell<ListNode<Thread>>,
    waiter: Waiter,
    join_waiters: WaitQueue,

    /// 线程是否正在运行在当前 CPU 上
    pub(in crate::kernel::thread) on_cpu: AtomicBool,

    // context 只能由持有调度器全局锁且禁止抢占的代码修改，不能通过普通
    // Thread API 取得可变引用
    context: SyncUnsafeCell<ArchThreadContext>,

    // 栈所有权与 context 分开保存，context 中的 rsp 始终指向这块内存
    _kernel_stack: KernelStack,
    inner: Spinlock<ThreadInner>,
}

impl Thread {
    /// 分配一个尚未注册、不可调度的内核线程。
    pub fn new_kernel(
        name: &'static CStr,
        entry: KernelThreadEntry,
        argument: *mut c_void,
    ) -> Result<Self, MemoryError> {
        let mut stack = KernelStack::new()?;
        let context = unsafe { ArchThreadContext::new_kernel(&mut stack, entry, argument) };

        Ok(Self {
            id: ThreadId::new(),
            name,
            run_node: SyncUnsafeCell::new(ListNode::new()),
            thread_node: SyncUnsafeCell::new(ListNode::new()),
            waiter: Waiter::new(),
            join_waiters: WaitQueue::new_uninit(),
            on_cpu: AtomicBool::new(false),
            context: SyncUnsafeCell::new(context),
            _kernel_stack: stack,
            inner: Spinlock::new(ThreadInner::new()),
        })
    }

    pub(in super::super) fn init(&self) {
        self.join_waiters.init();
    }

    pub(in super::super) fn prepare_first_thread(thread: &Self) -> ! {
        let context = unsafe { &*thread.context.get() };

        thread.transition_to(ThreadState::Running).unwrap();
        unsafe { ArchThreadContext::prepare_first_thread(context) }
    }

    pub const fn id(&self) -> ThreadId {
        self.id
    }

    pub const fn name(&self) -> &'static CStr {
        self.name
    }

    pub fn state(&self) -> ThreadState {
        self.inner.lock().state
    }

    pub(in super::super) fn transition_to(
        &self,
        new_state: ThreadState,
    ) -> Result<(), ThreadError> {
        self.inner.lock().transition_to(new_state)
    }

    /// 在唤醒 CPU 和切换收尾之间竞争取得 Waking -> Ready 的唯一提交权。
    pub(in super::super) fn try_waking_to_ready(&self) -> bool {
        let mut inner = self.inner.lock();
        if inner.state != ThreadState::Waking {
            return false;
        }

        inner
            .transition_to(ThreadState::Ready)
            .expect("Waking thread must transition to Ready");
        true
    }

    /// 等待该线程退出
    ///
    /// 线程退出是永久状态，因此允许多个持有者同时或先后等待。调用方必须持有
    /// 保证 `self` 在本次调用期间存活的强引用
    pub fn join(&self) {
        assert!(in_thread(), "Thread::join outside thread context");

        let state = self.state();
        assert!(state != ThreadState::Idle, "idle thread cannot be joined");
        assert!(can_preempt(), "Thread::join while preemption is disabled");

        if state == ThreadState::Dead {
            return;
        }

        {
            let preempt = PreemptGuard::new();
            assert!(
                !ptr::eq(scheduler(&preempt).current(), self),
                "thread cannot join itself"
            );
        }

        let condition = JoinCondition { thread: self };
        let _ = self.join_waiters.wait(&condition);
    }

    /// 发布永久退出状态并唤醒所有 joiner
    ///
    /// # Safety
    ///
    /// 必须已经关闭中断并持有可切换的 PreemptGuard，但不能持有 ready queue 锁，
    /// 因为唤醒 Blocked joiner 需要重新获取该锁
    ///
    /// 必须由即将退出的线程调用
    pub(in super::super) unsafe fn finish(&self, preempt: &PreemptGuard) {
        self.transition_to(ThreadState::Dead)
            .expect("running thread must transition to Dead");
        self.join_waiters.wake_all(preempt);
    }

    /// 切换架构上下文。仅供调度器在禁止抢占并独占上下文时调用
    ///
    /// # Safety
    ///
    /// `current` 和 `next` 必须是被调度器独占的线程，且在调用期间禁止抢占
    pub(in super::super) unsafe fn switch_context(current: &Self, next: &Self) {
        if ptr::eq(current, next) {
            return;
        }

        let current_context = unsafe { &mut *current.context.get() };
        let next_context = unsafe { &*next.context.get() };
        unsafe { current_context.switch_to(next_context) };
    }

    /// 获取线程在调度器就绪队列中的节点。仅供调度器在禁止抢占并独占上下文时调用。
    ///
    /// # Safety
    ///
    /// 该线程必须已经被 ThreadManager 注册
    pub(in super::super) unsafe fn get_run_node(&self) -> &mut ListNode<Thread> {
        unsafe { &mut *self.run_node.get() }
    }

    /// 获取线程在调度器就绪队列中的节点。仅供调度器在禁止抢占并独占上下文时调用。
    ///
    /// # Safety
    ///
    /// 该线程必须已经被 ThreadManager 注册
    pub(in super::super) unsafe fn get_thread_node(&self) -> &mut ListNode<Thread> {
        unsafe { &mut *self.thread_node.get() }
    }

    pub(in super::super) const fn run_node_offset() -> usize {
        offset_of!(Thread, run_node)
    }

    pub(in super::super) const fn waiter(&self) -> &Waiter {
        &self.waiter
    }

    /// 从内嵌的 Waiter 恢复所属线程。
    ///
    /// # Safety
    ///
    /// `waiter` 必须指向一个仍然存活的 Thread 的 `waiter` 字段。
    pub(in super::super) unsafe fn from_waiter(waiter: NonNull<Waiter>) -> NonNull<Self> {
        container_of!(waiter, Thread, waiter)
    }
}
pub struct ThreadInner {
    state: ThreadState,
}

impl ThreadInner {
    const fn new() -> Self {
        Self {
            state: ThreadState::New,
        }
    }

    fn transition_to(&mut self, new_state: ThreadState) -> Result<(), ThreadError> {
        match (self.state, new_state) {
            (ThreadState::New, ThreadState::Registered) => {
                self.state = ThreadState::Registered;
                Ok(())
            }
            (ThreadState::Registered, ThreadState::Ready) => {
                self.state = ThreadState::Ready;
                Ok(())
            }
            (ThreadState::Registered, ThreadState::Idle) => {
                self.state = ThreadState::Idle;
                Ok(())
            }
            (ThreadState::Ready, ThreadState::Running) => {
                self.state = ThreadState::Running;
                Ok(())
            }
            (ThreadState::Idle, ThreadState::Running) => {
                self.state = ThreadState::Running;
                Ok(())
            }
            (ThreadState::Running, ThreadState::Ready) => {
                self.state = ThreadState::Ready;
                Ok(())
            }
            (ThreadState::Running, ThreadState::Idle) => {
                self.state = ThreadState::Idle;
                Ok(())
            }
            (ThreadState::Running, ThreadState::Blocking) => {
                self.state = ThreadState::Blocking;
                Ok(())
            }
            (ThreadState::Running, ThreadState::Running) => {
                self.state = ThreadState::Running;
                Ok(())
            }
            (ThreadState::Blocking, ThreadState::Blocked) => {
                self.state = ThreadState::Blocked;
                Ok(())
            }
            (ThreadState::Blocking, ThreadState::Running) => {
                self.state = ThreadState::Running;
                Ok(())
            }
            (ThreadState::Blocked, ThreadState::Waking) => {
                self.state = ThreadState::Waking;
                Ok(())
            }
            (ThreadState::Waking, ThreadState::Ready) => {
                self.state = ThreadState::Ready;
                Ok(())
            }
            (ThreadState::Running, ThreadState::Dead) => {
                self.state = ThreadState::Dead;
                Ok(())
            }
            _ => Err(ThreadError::InvalidTransition {
                from: self.state,
                to: new_state,
            }),
        }
    }
}

struct JoinCondition<'a> {
    thread: &'a Thread,
}

impl WaitCondition for JoinCondition<'_> {
    type State = ThreadInner;

    fn condition_lock(&self) -> &Spinlock<Self::State> {
        &self.thread.inner
    }

    fn is_satisfied(&self, guard: &SpinIrqGuard<'_, &mut Self::State>) -> bool {
        guard.state == ThreadState::Dead
    }
}
