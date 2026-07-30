# Scheduler

目前的 `Scheduler` 只是对调度器的简单抽象，选用了时间片轮转调度作为暂时的调度算法占位

## 结构

调度器需要记录的线程有这些：

- 准备好被调度的线程
- 当前正在运行的线程
- idle线程
- 已退出等待被清理的线程

对应的结构如下：

```rust
pub struct Scheduler {
    /// 就绪队列，存放所有处于 Ready 状态的线程
    ready: Spinlock<ReadyQueue>,
    /// 当前正在运行的线程
    current: AtomicPtr<Thread>,
    /// 已经切离 CPU、等待下一个线程完成切换收尾的线程。
    previous: AtomicPtr<Thread>,
    /// 空闲线程，永远不会被调度器抢占
    ///
    /// 当需要切换到 idle 线程时，该字段会被设置为空，避免出现多个引用
    idle: AtomicPtr<Thread>,
    /// 已经退出调度系统、等待在下一个线程栈上释放 manager 引用的线程。
    pending_exit: AtomicPtr<Thread>,
    
    // ...
}
```

除此之外，还需要记录的信息有：

- 当前是否能够调度
- 是否需要调度
- 当前线程剩余的时间片

对应的结构是：

```rust
pub struct Scheduler {
    // ...
    
    /// 抢占计数，当计数为 0 时，当前线程可以被抢占
    preempt_count: AtomicU8,
    resched: AtomicBool,
    slice_ms: AtomicU16,
}
```

## 初始化

```rust
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

pub(super) fn init(&self, current: &'static Thread, idle: &'static Thread) {
    self.current
        .store(current as *const Thread as *mut Thread, Ordering::Relaxed);

    self.idle
        .store(idle as *const Thread as *mut Thread, Ordering::Relaxed);

    self.preempt_count.store(1, Ordering::Relaxed);

    let mut ready = self.ready_queue().lock_irqsave_pinned();
    unsafe { ready.as_mut().get_unchecked_mut() }.init();
}
```

抢占计数在初始化时设为了 1，即刚初始化时不允许抢占

## 工具函数

一些基础的工具函数

```rust
fn ready_queue(&self) -> Pin<&Spinlock<ReadyQueue>> {
    unsafe { Pin::new_unchecked(&self.ready) }
}

pub(super) fn enqueue(&self, thread: &Thread) -> Result<(), ThreadError> {
    unsafe {
        self.ready_queue()
            .lock_irqsave_pinned()
            .as_mut()
            .enqueue(thread)
    }
}

/// 尝试将一个已经从 WaitQueue 删除的 Waking 线程加入 ready queue。
///
/// # Safety
///
/// 调用方必须持有原 WaitQueue 的锁并关闭中断；thread 必须处于 Blocked，
/// 且它的 run node 当前未链接。
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

/// 检查当前线程是否可以被抢占。
pub(super) fn can_preempt(&self) -> bool {
    self.preempt_count.load(Ordering::Relaxed) == 0
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

fn idle(&self) -> &'static Thread {
    // SAFETY: idle 在线程管理器初始化期间注册，并且永远不会退出。
    unsafe {
        self.idle
            .load(Ordering::Relaxed)
            .as_ref()
            .expect("scheduler idle thread is not initialized")
    }
}
```

## 阻塞

线程阻塞和唤醒的完整协议位于 `WaitQueue::wait` 与 `wake_first`；调度器不再
提供单独的 `commit_block`。等待线程在等待队列锁内提交 `Blocking -> Blocked`，
随后选择下一个线程并切换。

## tick

`tick` 由每次产生时钟中断时调用，用来更新剩余时间片长度

```rust
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
```

## 抢占

发生抢占有两种情况，一种是由线程主动让出 CPU，也就是 `yield` ；另一种则是在等待重新调度时，由内核发起的在允许发生抢占的点抢占

```rust
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
```

## 调度

调度的流程其实就是选择下一个线程，然后切换上下文，所以我将更算法强相关的选择下一个线程通过 trait 进行了抽象

```rust
pub trait Schedule {
    /// 获取下一个线程，返回 None 表示当前线程继续运行
    ///
    /// `guard`: PreemptGuard
    fn get_next(&self, guard: &mut PreemptGuard) -> Option<&'static Thread>;
}
```

`get_next` 只负责从 ready queue 取出候选线程；当前线程应当切换到什么状态，
由普通调度、等待和退出路径分别处理。

---

在目前的调度算法中，实现如下，主要是做了对 idle 线程的特殊处理

```rust
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
        } else if self.is_idle(current) {
            return None;
        } else {
            self.idle()
        };

        unsafe { queue.as_mut().dequeue(next) };
        Some(next)
    }
}

```

---

当前实现的普通线程切换会先清除调度请求，并把非 idle 的当前线程从
`Running` 转为 `Ready` 后重新放入 ready queue；然后取出候选线程，转为
`Running`，最后在关中断窗口内通过 `PreemptGuard::switch_thread` 切换。
`get_next` 只负责取出并返回候选线程。

```rust
/// 执行线程切换
///
/// 切换成功返回 Some(())，否则返回 None
fn schedule(&self, guard: &mut PreemptGuard<'_>) -> Option<()> {
    self.resched.store(false, Ordering::Relaxed);

    let next = self.get_next(guard)?;

    let _interrupt = ArchInterrupt::save_and_disable();

    // SAFETY: 已关闭中断，两个线程都由调度器独占，
    // 且 PreemptGuard 会跨越架构上下文切换保持有效。
    let _ = unsafe { guard.switch_thread(next) };
    Some(())
}
```

`_interrupt` 利用了 `Drop` ，在生命周期结束时自动恢复中断状态

## 退出

线程退出的工作也不复杂：先禁止抢占，获取到下一个线程，关闭中断后将自己设为 `Dead` 状态并唤醒所有在等待自己结束的线程，最后进行上下文切换

```rust
/// 退出当前线程。线程进入 Dead 后不会再次成为调度候选。
pub(super) fn exit_self(&self) -> ! {
    assert!(interrupt::in_thread(), "thread_exit outside thread context");

    let mut guard = PreemptGuard::new(self);
    assert!(
        guard.can_switch(),
        "thread_exit while preemption is disabled"
    );

    let current = guard.current();

    let next = self
        .get_next(&mut guard)
        .expect("Trying to exit idle thread");

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
```
