# Preempt Guard

`PreemptGuard` 用来避免再其保护的区间内发生抢占，能减少开关中断的次数降低额外的开销

`PreemptGuard` 应该是仅限于单个 CPU 核心内的，一旦涉及跨 CPU 可能会需要 IPI 等操作，作为更开关中断的轻量化替代显然不合适。另外，防止了发生调度，也就意味着防止了线程迁移到另一个 CPU 核心，所以可以放心的使用 per-CPU 变量来实现

## 设计

首先使用一个 per-CPU 变量作为计数器用于 CPU 核心内的全局状态，为 `0` 表示可以抢占，非 `0` 表示不可抢占。利用计数器而非布尔值还有一个好处，那就是可以同时创建多个 `PreemptGuard` ，避免外层和内层实现打架

```rust
cpu_local!(
    /// 抢占计数，当计数为 0 时，当前线程可以被抢占
    pub static PREEMPT_COUNT: u8 = 0;
);
```

`cpu_local!` 宏用于将全局变量的定义转换为 per-CPU 变量

```rust
#[must_use = "the scheduler preemption count must be restored"]
pub struct PreemptGuard {
    can_switch: bool,
}

impl !Send for PreemptGuard {}
impl !Sync for PreemptGuard {}
```

`PreemptGuard` 还需要被用于防止在手动触发调度时被抢占，所以有一个 `can_switch` 字段，如果创建 `PreemptGuard` 后计数为 1 则证明可以安全的切换线程。由于处于同一个 CPU 核心内所以不用担心中途被改变，即使是发生了中断，在结束之后也会恢复到原来的计数。

同时 `PreemptGuard` 也不允许跨线程转移所有权或者共享

## 实现

### 创建

```rust
pub fn new() -> Self {
    let previous = PREEMPT_COUNT.fetch_add(1);

    Self {
        can_switch: previous == 0,
    }
}
```

创建的时候使用了 `fetch_add` 来获取加之前的值，这里的 `fetch_add` 不是原子操作，在 x86 下使用 `XADD` 指令来实现，避免 CPU 锁总线同步造成额外开销

### 跨越线程边界

前面说了，`PreemptGuard` 的所有权是不允许跨线程转移或者共享的，为了使抢占计数在跨越线程边界时保持不变，又定义了 `into_raw` 和 `from_raw` 两个方法

例如，在使用 `schedule()` 切换线程时：

```rust
fn schedule(guard: PreemptGuard) -> Option<()> {
    // 获取下一个线程
    // ...
    
    guard.into_raw()
    
    unsafe { Thread::switch_context(current, next) }
    
    let guard = unsafe { PreemptGuard::from_raw() };
    
    // 剩余工作
    // ...
}
```

在切换前使用 `into_raw` 释放所有权，切换完成准备回到线程内的时候，会需要响应的使用 `from_raw` 然后自动 `drop` 减小引用计数恢复抢占

```rust
/// 将 guard 转换为由当前 CPU 抢占计数表示的 raw 状态。
///
/// 该操作只允许紧邻上下文切换使用；调用后必须由目标栈通过
/// `from_raw` 恢复唯一的 RAII guard。
pub(super) fn into_raw(self) {
    assert!(
        self.can_switch,
        "only a switch-capable guard can be transferred"
    );
    assert_eq!(
        PREEMPT_COUNT.read(),
        1,
        "raw preempt handoff requires exactly one outstanding guard"
    );
    mem::forget(self);
}

/// 接管上下文切换时由前一个线程栈传递的 raw 抢占状态。
///
/// # Safety
///
/// 当前 CPU 的抢占计数必须恰好包含一次由 `into_raw` 留下的未配平计数，
/// 且该 raw 状态尚未被其他 guard 接管。
pub(super) unsafe fn from_raw() -> Self {
    assert_eq!(PREEMPT_COUNT.read(), 1, "invalid raw preempt handoff");
    Self { can_switch: true }
}

```

### 其他

其余的都是做校验的简单封装

```rust
/// 检查当前是否可以切换上下文
pub fn can_switch(&self) -> bool {
    self.can_switch
}

/// 永久切离已经进入 Dead 状态的当前线程。
pub fn exit_current(self) -> ! {
    assert!(
        crate::kernel::interrupt::in_thread(),
        "thread_exit outside thread context"
    );
    assert!(
        self.can_switch(),
        "thread_exit while preemption is disabled"
    );

    Scheduler::exit_current(self)
}

pub fn try_preempt(self, _point: PreemptPoint) {
    if RESCHED.read() == 0 || !self.can_switch() {
        return;
    }

    let _ = Scheduler::schedule(self);
}

pub fn try_yield(self, _point: PreemptPoint) {
    if !self.can_switch() {
        return;
    }

    let _ = Scheduler::schedule(self);
}
```

### Drop

`drop` 使用了 `enable_preempt`，会在真正恢复抢占时尝试处理掉重新调度的请求

```rust
impl Drop for PreemptGuard {
    fn drop(&mut self) {
        unsafe { enable_preempt() };
    }
}

pub fn can_preempt() -> bool {
    PREEMPT_COUNT.read() == 0
}

pub unsafe fn disable_preempt() {
    assert!(PREEMPT_COUNT.read() < u8::MAX, "preempt count overflow");
    PREEMPT_COUNT.increase();
}

pub unsafe fn enable_preempt() {
    assert!(PREEMPT_COUNT.read() > 0, "unbalanced enable_preempt");
    let previous = PREEMPT_COUNT.fetch_sub(1);

    // 如果之前的计数为 1，说明当前线程已经可以被抢占了，并且有调度请求挂起，那么就尝试进行抢占。
    if previous == 1 && RESCHED.read() != 0 {
        if let Some(point) = PreemptPoint::new() {
            point.try_preempt(PreemptGuard::new());
        }
    }
}
```

