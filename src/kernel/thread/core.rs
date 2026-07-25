use alloc::boxed::Box;
use core::{
    cell::SyncUnsafeCell,
    ffi::{CStr, c_void},
    mem::offset_of,
    pin::Pin,
    ptr::{self, NonNull},
    sync::atomic::{AtomicUsize, Ordering},
};

use crate::{
    arch::{ArchPageTable, ArchThreadContext},
    kernel::{
        memory::{
            MemoryError,
            arch::ArchMemory,
            frame::buddy::FrameOrder,
            kmalloc::Kmalloc,
            page::{Pages, options::PageAllocOptions},
        },
        thread::{
            scheduler::scheduler,
            wait_queue::{WaitCondition, WaitQueue, Waiter},
        },
    },
    lib::rust::{
        list::ListNode,
        spinlock::{SpinIrqGuard, Spinlock},
    },
};

static NEXT_THREAD_ID: AtomicUsize = AtomicUsize::new(0);

const THREAD_STACK_ORDER: FrameOrder = FrameOrder::new(2);

pub type KernelThreadEntry = extern "C" fn(*mut c_void);

/// 架构层必须提供的最小线程上下文接口。
///
/// 寄存器布局、初始 switch frame 和 trampoline 均由架构实现私有管理。线程核心
/// 只提供一段已分配的内核栈，并保存返回的 opaque context。
pub trait ThreadContext: Sized {
    /// 在 `stack_bottom..stack_bottom + stack_size` 中构造新内核线程的初始帧。
    ///
    /// # Safety
    ///
    /// 栈范围必须独占、可写，并且至少在线程对象存活期间保持有效。
    unsafe fn new_kernel(
        stack: &mut KernelStack,
        entry: KernelThreadEntry,
        argument: *mut c_void,
    ) -> Self;

    /// 保存当前上下文并恢复 `next`。
    ///
    /// # Safety
    ///
    /// 调用者必须保证当前 CPU 独占两个上下文，且满足架构切换所需的中断和抢占约束。
    unsafe fn switch_to(&mut self, next: &Self);

    /// 为第一个线程构造初始上下文。
    ///
    /// # Safety
    ///
    /// 只能在构造第一个线程时使用，否则会破坏当前线程的上下文
    unsafe fn prepare_first_thread(context: &ArchThreadContext);
}

pub struct Thread {
    id: ThreadId,
    name: &'static CStr,

    run_node: SyncUnsafeCell<ListNode<Thread>>,
    waiter: Waiter,
    join_waiters: Pin<Box<WaitQueue, Kmalloc>>,

    // context 只能由持有调度器全局锁且禁止抢占的代码修改，不能通过普通
    // Thread API 取得可变引用。
    context: SyncUnsafeCell<ArchThreadContext>,

    // 栈所有权与 context 分开保存；context 中的 rsp 始终指向这块内存。
    _kernel_stack: KernelStack,
    inner: Spinlock<ThreadInner>,
}

impl Thread {
    /// 分配一个尚未注册、不可调度的内核线程。
    pub(super) fn new_kernel(
        name: &'static CStr,
        entry: KernelThreadEntry,
        argument: *mut c_void,
    ) -> Result<Self, MemoryError> {
        let mut stack = KernelStack::new()?;
        let context = unsafe { ArchThreadContext::new_kernel(&mut stack, entry, argument) };
        let join_waiters = WaitQueue::try_new()?;

        Ok(Self {
            id: ThreadId::new(),
            name,
            run_node: SyncUnsafeCell::new(ListNode::new()),
            waiter: Waiter::new(),
            join_waiters,
            context: SyncUnsafeCell::new(context),
            _kernel_stack: stack,
            inner: Spinlock::new(ThreadInner::new()),
        })
    }

    pub(super) fn prepare_first_thread(thread: &Self) {
        let context = unsafe { &*thread.context.get() };

        thread.transition_to(ThreadState::Running).unwrap();
        unsafe { ArchThreadContext::prepare_first_thread(context) };
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

    pub(super) fn transition_to(&self, new_state: ThreadState) -> Result<(), ThreadError> {
        self.inner.lock().transition_to(new_state)
    }

    /// 等待该线程退出。
    ///
    /// 线程退出是永久状态，因此允许多个持有者同时或先后等待。调用方必须持有
    /// 保证 `self` 在本次调用期间存活的强引用。
    pub fn join(&self) {
        assert!(
            crate::kernel::interrupt::in_thread(),
            "Thread::join outside thread context"
        );

        let scheduler = scheduler();
        assert!(
            !ptr::eq(scheduler.current(), self),
            "thread cannot join itself"
        );
        assert!(!scheduler.is_idle(self), "idle thread cannot be joined");
        assert!(
            scheduler.can_preempt(),
            "Thread::join while preemption is disabled"
        );

        let condition = JoinCondition { thread: self };
        let _ = self.join_waiters.as_ref().wait(&condition);
    }

    /// 发布永久退出状态并唤醒所有 joiner。
    ///
    /// 调用方必须已经关闭中断并持有可切换的 PreemptGuard，但不能持有 ready
    /// queue 锁，因为唤醒 Blocked joiner 需要重新获取该锁。
    pub(super) fn finish(&self) {
        self.transition_to(ThreadState::Dead)
            .expect("running thread must transition to Dead");
        self.join_waiters.as_ref().wake_all();
    }

    /// 切换架构上下文。仅供调度器在禁止抢占并独占上下文时调用。
    ///
    /// # Safety
    ///
    /// `current` 和 `next` 必须是被调度器独占的线程，且在调用期间禁止抢占。
    pub(super) unsafe fn switch_context(current: &Self, next: &Self) {
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
    pub(super) unsafe fn get_run_node(&self) -> Pin<&mut ListNode<Thread>> {
        unsafe { Pin::new_unchecked(&mut *self.run_node.get()) }
    }

    pub(super) const fn run_node_offset() -> usize {
        offset_of!(Thread, run_node)
    }

    pub(super) const fn waiter(&self) -> &Waiter {
        &self.waiter
    }

    /// 从内嵌的 Waiter 恢复所属线程。
    ///
    /// # Safety
    ///
    /// `waiter` 必须指向一个仍然存活的 Thread 的 `waiter` 字段。
    pub(super) unsafe fn from_waiter(waiter: NonNull<Waiter>) -> NonNull<Self> {
        crate::container_of!(waiter, Thread, waiter)
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

pub extern "C" fn thread_entry_wrapper(entry: KernelThreadEntry, argument: *mut c_void) -> ! {
    let scheduler = scheduler();
    unsafe { scheduler.finish_first_switch() };
    entry(argument);
    scheduler.exit_self()
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
                self.register();
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
            (ThreadState::Blocking, ThreadState::Blocked) => {
                self.state = ThreadState::Blocked;
                Ok(())
            }
            (ThreadState::Blocking, ThreadState::Running) => {
                self.state = ThreadState::Running;
                Ok(())
            }
            (ThreadState::Blocked, ThreadState::Ready) => {
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

    fn register(&mut self) {
        self.state = ThreadState::Registered;
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(transparent)]
pub struct ThreadId(usize);

impl ThreadId {
    fn new() -> Self {
        Self(NEXT_THREAD_ID.fetch_add(1, Ordering::Relaxed))
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ThreadState {
    New,
    Registered,
    Idle,
    Ready,
    Running,
    Blocking,
    Blocked,
    Dead,
}

#[derive(Debug)]
pub enum ThreadError {
    InvalidTransition { from: ThreadState, to: ThreadState },
}

#[repr(transparent)]
pub struct KernelStack {
    pages: Pages,
}

unsafe impl Sync for KernelStack {}
unsafe impl Send for KernelStack {}

impl KernelStack {
    fn new() -> Result<Self, MemoryError> {
        let pages = PageAllocOptions::kernel(THREAD_STACK_ORDER)
            .zeroed(true)
            .allocate()?;

        Ok(Self { pages })
    }

    pub fn top(&self) -> NonNull<u8> {
        let bottom = self.bottom();
        unsafe { bottom.byte_add(self.size()) }
    }

    pub fn bottom(&self) -> NonNull<u8> {
        self.pages.get_ptr()
    }

    const fn size(&self) -> usize {
        self.pages.get_count() * ArchPageTable::PAGE_SIZE
    }
}
