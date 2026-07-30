除了 `Thread` 之外，还有一个抽象 `ThreadManager` 用于管理线程。`Thread` 可以直接在栈上创建，但是此时还是不能运行的，需要向 `ThreadManager` 注册自己，由其为线程分配一个固定的内存将线程交给调度器才能被运行

# ThreadManager

`ThreadManager` 目前只在两个时机发挥作用：将运行线程时将持有线程的强引用、线程退出时将强引用返还。由此保证了线程的生命周期不会短于其运行时长，且顺便完成了一些结构的初始化工作，避免将这一工作交给调用方的风险

## 结构

首先全局只有一个 `ThreadManager`

```rust
pub static THREAD_MANAGER: ThreadManager = ThreadManager {
    all: Spinlock::new(ListHead::empty()),
};

pub struct ThreadManager {
    all: Spinlock<ListHead<Thread>>,
}
```

## 函数

通过 `register` 为一个线程描述结构分配一段固定内存并初始化

```rust
/// 创建并持有一个已注册但尚不可调度的内核线程
pub fn register(&self, thread: Thread) -> Result<ThreadArc, MemoryError> {
    let thread_ref = ThreadArc::try_new_in(thread, Kmalloc::default())
        .map_err(|_| MemoryError::OutOfMemory)?;

    let (thread, _) = Arc::into_raw_with_allocator(thread_ref.clone());

    // SAFETY: 此时实际上只有一个可变引用，且不可变引用不会被使用
    let thread = unsafe { &mut *(thread as *mut Thread) };

    thread.init();

    thread
        .transition_to(ThreadState::Registered)
        .expect("new manager-owned thread must be in New state");

    unsafe {
        self.all.init_with(|list_head| {
            list_head.init();
            list_head.add_tail(thread.get_thread_node());
        })
    };

    Ok(thread_ref)
}
```

`remove` 则是在线程已经真正停止运行后将其清理掉

```rust
/// 移除已经退出调度系统的线程，并返回 manager 持有的强引用
///
/// 返回值必须在 manager 锁外、且不再运行于该线程的内核栈上时释放
pub(super) fn remove(&self, thread: &Thread) -> ThreadArc {
    assert!(
        thread.state() == ThreadState::Dead,
        "only exited thread can be removed"
    );
    unsafe {
        // SAFETY: THREAD_MANAGER 是全局变量，是 Pin 的，且在整个系统生命周期内不会被释放
        let all = Pin::new_unchecked(&self.all);
        let mut all = all.lock_pinned();

        all.as_mut().delete_pinned(thread.get_thread_node());

        // SAFETY: 只要 thread 在 ThreadManager 的 all 链表中，它就一定是由 ThreadManager 持有的 Arc
        Arc::from_raw_in(thread, Kmalloc::default())
    }
}
```

