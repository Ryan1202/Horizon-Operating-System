目前大部分的代码还是使用 C 写的，所以线程子系统还是要保留一套给 C 设计的接口

# Thread

得益于之前将线程创建和运行分开的设计，C 侧的强引用/弱引用管理及其使用便利性可以兼顾

## 创建

创建一个还不能运行的线程

```rust
/// 创建一个由 ThreadManager 持有、尚未加入运行队列的线程。
///
/// 返回值是 borrowed pointer；若 C 需要在线程运行后继续持有它，必须在
/// thread_run 前调用 thread_get 获取 owning handle。
#[unsafe(export_name = "thread_create")]
extern "C" fn thread_create_c(
    name: *const i8,
    entry: extern "C" fn(*mut c_void),
    argument: *mut c_void,
) -> *const Thread {
    let name = unsafe { CStr::from_ptr(name) };
    let Ok(thread) = Thread::new_kernel(name, entry, argument)
        .and_then(|thread| THREAD_MANAGER.register(thread))
    else {
        return null();
    };

    thread.as_ref()
}
```

返回的指针是借用指针，不计入 C 侧引用计数；ThreadManager 保留自己的强引用，
直到线程退出并完成切换收尾。C 侧若要跨越 `thread_run` 或线程运行期保存它，
必须先调用 `thread_get`，并最终调用 `thread_put`。

## 获取强引用

如果 C 侧需要保证线程不会随时被释放，则需要在线程运行前先获取其强引用

```rust
/// 为 thread_create 返回的 borrowed pointer 获取一个 owning handle。
#[unsafe(export_name = "thread_get")]
extern "C" fn thread_get_c(thread: *const Thread) -> *const Thread {
    let Some(thread_ref) = (unsafe { thread.as_ref() }) else {
        return null();
    };

    assert!(interrupt::in_thread(), "thread_get outside thread context");
    assert_eq!(
        thread_ref.state(),
        crate::kernel::thread::ThreadState::Registered,
        "borrowed thread pointer must be retained before thread_run"
    );

    // SAFETY: Registered 线程由 ThreadManager 持有，pointer 来自对应的 Arc。
    unsafe { Arc::<Thread, Kmalloc>::increment_strong_count_in(thread, Kmalloc::default()) };

    thread
}
```

由于不需要实际使用引用，直接增加一个强引用计数

## 释放强引用

```rust
/// 释放一个由 thread_get 返回的 owning handle。
#[unsafe(export_name = "thread_put")]
extern "C" fn thread_put_c(thread: *const Thread) {
    if thread.is_null() {
        return;
    }

    assert!(interrupt::in_thread(), "thread_put outside thread context");

    // SAFETY: C 调用方必须传入一个 owning handle，并且每个 handle 只调用一次。
    let _: ThreadArc = unsafe { Arc::from_raw_in(thread, Kmalloc::default()) };
}
```

将指针转换回 `Arc` 并 drop

## 运行线程

```rust
/// 将一个由 thread_create 创建的 Registered 线程加入运行队列。
#[unsafe(export_name = "thread_run")]
extern "C" fn thread_run_c(thread: *const Thread) -> bool {
    let Some(thread) = (unsafe { thread.as_ref() }) else {
        return false;
    };

    {
        let thread = unsafe {
            ManuallyDrop::new(Arc::<Thread, Kmalloc>::from_raw_in(
                thread,
                Kmalloc::default(),
            ))
        };
        assert!(
            Arc::strong_count(&thread) > 0,
            "thread_run called on unretained thread"
        );
    }

    assert!(interrupt::in_thread(), "thread_run outside thread context");
    scheduler().enqueue(thread).is_ok()
}
```

由于这里不能影响引用计数，所以使用了 `ManuallyDrop` 避免自动 drop `Arc`

## 等待线程退出

```rust
/// 等待 owning handle 指向的线程退出，不消费该引用。
#[unsafe(export_name = "thread_join")]
extern "C" fn thread_join_c(thread: *const Thread) {
    unsafe { thread.as_ref() }
        .expect("null thread handle")
        .join();
}
```

就是 `join` 的包装

## 退出线程

```rust
#[unsafe(export_name = "thread_exit")]
extern "C" fn thread_exit_c() -> ! {
    scheduler().exit_self()
}
```

只能结束自己
