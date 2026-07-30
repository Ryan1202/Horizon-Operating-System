# 定义

```rust
#[repr(C)]
pub struct WaitQueue {
    inner: Spinlock<WaitQueueInner>,
}

#[repr(C)]
struct WaitQueueInner {
    waiters: ListHead<Waiter>,
    initialized: bool,
}
```

`WaitQueue` 本身是一个不可变的类型，通过 `Spinlock` 实现内部可变性，会发生改变的部分都放进了 `WaitQueueInner`

线程内嵌的 `join_waiters` 使用 `new_uninit`，由 `ThreadManager::register` 调用
线程的 `init` 初始化；独立分配的队列使用 `WaitQueue::try_new`，返回固定地址的
`Pin<Box<WaitQueue, Kmalloc>>`。队列销毁前必须没有活动 waiter。

---

由于 `WaitQueue` 是作为类似底座的存在，所以通过 trait 抽象了各种等待条件

```rust
/// 描述由某把自旋锁保护的等待条件。
pub trait WaitCondition {
    type State: Unpin;

    /// 返回保护条件状态的锁。
    fn condition_lock(&self) -> &Spinlock<Self::State>;

    /// 在持有条件锁时判断条件是否已经满足。
    fn is_satisfied(&self, guard: &SpinIrqGuard<'_, &mut Self::State>) -> bool;
}
```

# 设计

设两个线程：

- A：等待条件成立。
- B：修改条件并唤醒 A。

## 一

```
线程 A                         线程 B
检查条件：false
                              修改条件：true
                              wake()：队列为空
进入等待队列
阻塞
```

此时线程 A 会睡死，可能永远也不会唤醒

## 二

如果给条件加上锁

```
线程 A                         线程 B
lock(condition)
检查：false
unlock(condition)
                              lock(condition)
                              条件 = true
                              unlock(condition)
                              wake()：队列为空
enqueue(A)
阻塞
```

此时可以发现 B 其实无法保证将 A 出队，所以需要把出入队也囊括进临界区

## 三

条件锁应该只能负责保护条件，所以需要使用另外的队列锁

```
线程 A                         线程 B
lock(condition)
检查：false
lock(waitqueue)
unlock(condition)
                              lock(condition)
                              条件 = true
enqueue(A)
unlock(waitqueue)
                              lock(waitqueue)
                              unlock(condition)
                              wake(A)
                              unlock(waitqueue)
阻塞
```

通过两个锁的交叉保证中间是连贯的

这时 wake 可以保证将 A 出队了，但是 A 还是会照常阻塞。但是又不能把阻塞操作放进锁内，这样锁就无法释放把 B 也给阻塞住了

## 四

如果 A 在阻塞前检查自己是否还在队内呢？同样也不行，因为这和阻塞操作之间还是有可能被打断，也同样不能用锁保护否则会无法释放

所以我引入了一个中间状态 `Blocking` ，A 在入队后解锁前将自己设为 `Blocking` ，B 如果需要唤醒就将其设置回 `Running` (`Ready` 行不行？)

A 在阻塞时必须要将自己的状态设置为 `Blocked` ，而只有 `Blocking` 状态能转换到 `Blocked` ，所以如果不是 `Blocking` 就不阻塞恢复运行

```
线程 A                         线程 B
lock(condition)
  检查：false
    true => 返回
  lock(waitqueue)
unlock(condition)
                              lock(condition)
                                条件 = true
                              unlock(condition)
    lock(state)
      Running → Blocking
    unlock(state)
    enqueue(A)
  unlock(waitqueue)
                                lock(waitqueue)
                                  lock(state)
                                    Blocking → Running
                                  unlock(state)
                                  wake(A)
                                unlock(waitqueue)
lock(waitqueue)
  lock(state)
    发现自己已经是 Running
  unlock(state)
  dequeue(A)
unlock(waitqueue)
回到开头重新检查
```

这解决了“入队后、阻塞前”的竞态

但是这个流程成功阻塞的情况又会出问题，如果先释放锁再阻塞，中间就可能被打断，然后状态变回了 `Ready` 但是调用了阻塞把自己移出了调度队列，如果先阻塞了那锁又释放不了了

## 五

所以我引入了一个新的状态 `Waking` 表示可以被唤醒了，处于这个状态是该线程还不在调度队列中。同时，线程描述结构中还需要增加一个 `on_cpu` 表示该线程是否还在真正运行。

如果还在运行，那么唤醒方只将其设为 `Waking` 状态，由该线程切换后的收尾路径
将其恢复 `Ready` 并加入调度队列；

否则，唤醒方将其设为 `Waking` 后，直接将其恢复 `Ready` 放回到调度队列

此时，成功阻塞的流程如下

```
线程 A                         线程 B
...                           ...
lock(waitqueue)
  lock(state)
    自己是 Blocking
  unlock(state)
  Blocking → Blocked
unlock(waitqueue)
                            （可能性1）
                             lock(waitqueue)
                               lock(state)
                                 A: Blocked → Waking
                               unlock(state)
                               dequeue(A)
                               发现 on_cpu == true，结束
                             unlock(waitqueue)
获取下一个线程
切换上下文
on_cpu=false
                            （可能性2）
                             lock(waitqueue)
                               lock(state)
                                 A: Blocked → Waking
                               unlock(state)
                               dequeue(A)
                               if on_cpu == false
                                 A: Waking → Ready
                                 scheduler.enqueue(A)
                             unlock(waitqueue)
```

相当于将线程状态作为仲裁机制，成功将线程状态转换为 `Waking` 的一方负责将线程入队

# 实现

## 等待

由于 `WaitQueue` 的定位是基座，所以 `wait` 接受一个实现了 `WaitCondition` 的变量输入，并且会将条件锁的 `guard` 返回，也就是将释放时机交给调用方决定，保证条件变量被连续地保护

```rust
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
        /// ...
    }
}
```

核心逻辑都在死循环 `loop` 中。在最开始先检查一遍条件提供快速返回路径

```rust
let condition_guard = condition.condition_lock().lock_irqsave();
if condition.is_satisfied(&condition_guard) {
    return condition_guard;
}
```

正式进入等待队列的管理是需要关闭抢占和中断保证当前线程占有当前CPU，`SpinIrqGuard::downgrade()` 可以将一个同时管中断和锁的 `guard` 拆分成两个 `guard` ，因为条件锁需要及时释放但中断不能开

```rust
let (condition_guard, _interrupt) = condition_guard.downgrade();

let scheduler = scheduler();
let mut preempt = PreemptGuard::new(&scheduler);
assert!(
    preempt.can_switch(),
    "WaitQueue::wait while preemption is disabled"
);

let current = preempt.current();
assert!(!scheduler.is_idle(current), "idle thread must not wait");
```

第一阶段，线程进入 `Blocking` 状态，加入等待队列，此时线程可以被另一个线程 ”唤醒“ 了

```rust
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
```

第二阶段，决定线程是否需要阻塞，如果被恢复成了 `Running` 就回到循环开头走快速返回路径，否则就决定进入阻塞状态

```rust
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
```

经过第二阶段后，线程就无法阻止的进入阻塞状态了，如果要提前唤醒也必须等线程保存完上下文后走调度器把它唤起了

```rust
// SAFETY: IRQ 已关闭，current 已经提交为 Blocked，next 已经成为
// Running，且 PreemptGuard 跨越上下文切换保持有效。
let next = scheduler.get_next(&mut preempt).unwrap_or(scheduler.idle());
next.transition_to(ThreadState::Running)
    .expect("next thread must transition to Running");
let _exited = unsafe { preempt.switch_thread(next) };

// 先恢复中断再释放 _exited，避免阻塞中断处理
drop(_interrupt);
```

`switch_thread` 返回就说明该线程已经被唤醒了，此时需要先手动 `drop` 来恢复中断，因为 `_exited` 可能是上一个线程退出留下来的残局需要处理，不能让其阻塞中断处理

## 唤醒

唤醒的核心是 `wake_first` ，唤醒等待队列里的第一个线程

```rust
/// 唤醒列表里第一个线程
///
/// 返回该 waiter 是否被加入 ready queue。Blocking waiter 仍是 current，
/// 只需取消阻塞，不得重复加入 ready queue。
fn wake_first<'a>(guard: &mut SpinIrqGuard<'a, Pin<&'a mut Self>>) -> Option<bool> {
    let waiter_ptr = guard.first()?;
    let waiter = unsafe { waiter_ptr.as_ref() };
    let thread = unsafe { Thread::from_waiter(waiter_ptr).as_ref() };

    match thread.state() {
        ThreadState::Blocking => {
            guard.as_mut().remove(waiter);
            thread
                .transition_to(ThreadState::Running)
                .expect("Blocking thread must return to Running");
            Some(false)
        }
        ThreadState::Blocked => {
            guard.as_mut().remove(waiter);
            thread
                .transition_to(ThreadState::Waking)
                .expect("Blocked thread must transition to Waking");

            let made_ready =
                !thread.on_cpu.load(Ordering::Acquire) && scheduler().try_enqueue_woken(thread);
            Some(made_ready)
        }
        state => panic!("Waiter belongs to a non-waiting thread: {state:?}"),
    }
}
```

要求输入 `SpinIrqGuard` ，保证当前 `WaitQueue` 已上锁并关中断

对于 `Blocking` 状态，可以打断其阻塞过程，将其恢复为 `Running`；对于 `Blocked`
已无法阻止其阻塞，所以将其设置为 `Waking`，如果该线程已离开 CPU 则尝试通过
`Scheduler::try_enqueue_woken` 直接入队，否则由它的下一个线程处理。状态转换和
ready-queue 入队在同一个 ready-queue 临界区内完成，避免双方重复入队。

---

在 `wake_first` 基础上，实现了两个对外的接口 `wake_one` 和 `wake_all`

```rust
/// 唤醒 FIFO 队首的一个 waiter。
pub fn wake_one(&self) -> bool {
    let mut waiters = self.waiters().lock_irqsave_pinned();

    let Some(made_ready) = WaitQueueInner::wake_first(&mut waiters) else {
        return false;
    };
    if made_ready {
        scheduler().request_resched();
    }
    true
}

/// 唤醒当前队列中的全部 waiter，返回从队列移除的数量。
pub fn wake_all(&self) -> usize {
    let mut waiters = self.waiters().lock_irqsave_pinned();
    let mut count = 0;
    let mut made_ready = false;

    while let Some(ready) = WaitQueueInner::wake_first(&mut waiters) {
        count += 1;
        made_ready |= ready;
    }
    if made_ready {
        scheduler().request_resched();
    }
    count
}
```




