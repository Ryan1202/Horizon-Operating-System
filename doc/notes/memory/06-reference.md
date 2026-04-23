前面提到了 `FrameRc` 引用计数，我在这引用计数的基础上，结合了 `Rust` 的所有权设计了两种引用：独占引用 ( `UniqueFrames` ) 和 共享引用 ( `SharedFrames` )。同时利用了 Rust 的自动 Drop 机制，每次 Drop 减少引用计数，当没有其他引用时，自动调用释放物理页的函数。

# 独占引用

当引用计数为 0 时，视作这个 `Frame` 被独占了，其他线程最多是（不安全的）读取一下值，不能做其他操作。独占引用有一个专门的类型 `UniqueFrames` :

```rust
pub struct UniqueFrames {
    start: NonNull<Frame>,
    order: FrameOrder,
}
```

一个 `UniqueFrames` 可以表示连续的一组物理页，同样也是以 `FrameOrder` 为单位，`start` 是指向头一个 `Frame` 的指针。

## 安全的创建

由于所有的 `Frame` 都来自于分配器的分配，所以唯一安全的创建方式是 `from_allocator` ，在函数的参数里要求了一个实现了 `FrameAllocator` 的类型，所以只能在分配器内部调用：

```rust
/// 从信任的分配器获取独占访问权限，成功时返回 `UniqueFrames`
///
/// 由分配器指定帧 order
pub(super) fn from_allocator<A: FrameAllocator>(
    frame: NonNull<Frame>,
    order: FrameOrder,
    _allocator: &A,
) -> Option<ManuallyDrop<Self>> {
    // 根据 order 对齐帧地址
    let frame_number = FrameNumber::from_frame(frame);
    let start = frame_number.align_down(order);
    let start = Frame::get_raw(start);

    let frame_ref = unsafe { frame.as_ref() };
    debug_assert_eq!(frame_ref.refcount.get(), FrameRc::EXCLUSIVE);

    match frame_ref.get_tag() {
        FrameTag::Buddy | FrameTag::Free | FrameTag::Uninited => {
            Some(ManuallyDrop::new(UniqueFrames { start, order }))
        }
        FrameTag::Tail => {
            let range = unsafe { frame_ref.get_data().range };

            let head = range.start;
            let head_frame = Frame::get_raw(head);
            let head_ref = unsafe { head_frame.as_ref() };
            if matches!(
                head_ref.get_tag(),
                FrameTag::Buddy | FrameTag::Free | FrameTag::Uninited
            ) && frame_number + order.to_count().get() <= range.end
            {
                Some(ManuallyDrop::new(UniqueFrames { start, order }))
            } else {
                None
            }
        }
        _ => None,
    }
}
```

还未被分配的时候，头一个物理页可能是 `Buddy` , `Free` , `Uninited` 三种状态，如果不是头一个物理页，则是 `Tail` ，那么就通过 `FrameRange` 获取到范围信息验证传入的范围是否有效后返回引用

## 不安全的创建

由于在将分配结果从 Rust 传递到 C 后就超出所有权的管理范围了，所以需要先通过 `mem::foget` 避免自动释放，此时引用计数不会变化，然后在 C 调用释放函数时不排斥独占引用的同时创建 `UniqueFrames`

```rust
/// 尝试从原始指针创建 `UniqueFrames` 而不验证引用计数
///
/// # Safety
///
/// 调用者必须确保该帧当前没有其他引用，否则可能导致数据竞争和未定义行为
pub unsafe fn try_from_raw(mut frame: NonNull<Frame>) -> Option<Self> {
    let frame_ref = unsafe { frame.as_mut() };
    if frame_ref.refcount.is_exclusive(frame_ref) {
        Some(UniqueFrames {
            start: frame,
            order: FrameOrder::new(0),
        })
    } else {
        None
    }
}
```

## 合并

这是为 Buddy 提供方便的工具函数，用于将两个 `FrameOrder` 相同且连续的 `UniqueFrames` 合并成一个 `UniqueFrames`

```rust
pub fn merge<A, F>(
    mut low: ManuallyDrop<Self>,
    mut high: ManuallyDrop<Self>,
    allocator: &A,
    f: F,
) -> Result<ManuallyDrop<Self>, (ManuallyDrop<Self>, ManuallyDrop<Self>)>
where
    A: FrameAllocator,
    F: FnOnce(&A, &mut Self, ManuallyDrop<Self>),
{
    let low_ = low.deref_mut();
    let high_ = high.deref_mut();

    if low_.order != high_.order || low_.get_tag() != high_.get_tag() {
        return Err((low, high));
    }

    let expected = low_.to_frame_number() + (1 << low_.order.get());
    if expected != high_.to_frame_number() {
        return Err((low, high));
    }

    f(allocator, low_, high);
    Ok(low)
}
```

`ManuallyDrop` 类型的变量不会自动 Drop，需要手动调用将内部的类型取出来再让语言自动 Drop。这里不管理被合并的物理页的生命周期，交给分配器传入的 `f` 自己去处理，然后将合并后的物理页所有权返回

## 分割

和合并差不多，不同点在于分割后新创建的引用用 `ManuallyDrop` 包裹，交给分配器自己管理生命周期

```rust
pub fn split(&mut self) -> ManuallyDrop<Self> {
    debug_assert!(self.order.get() > 0, "Cannot split a frame of order 0");

    let new_order = self.order - 1;
    self.order = new_order;

    let next_frame_number = self.to_frame_number() + (1 << new_order.get());
    let next_frame = Frame::get_raw(next_frame_number);

    ManuallyDrop::new(Self {
        start: next_frame,
        order: new_order,
    })
}
```

## 设置大页

为了减少性能开销，大页除了头一个物理页，其余的都只在被分配后才配置信息，所以 `UniqueFrames` 也提供了一个函数自动设置

```rust
pub(super) fn set_tail_frames(&mut self) {
    let count = self.order.to_count().get();

    let start = FrameNumber::from_frame(self.start);
    let end = start + count;
    let range = FrameRange { start, end };

    unsafe {
        for i in 1..count {
            let frame = Frame::get_raw(start + i).as_mut();

            frame.replace(FrameTag::Tail, FrameData { range });
        }
    }
}
```

## 转换

当然，独占引用也需要一个直接转换成共享引用的函数，只需将 `FrameRc` 设为 1 即可。

```rust
pub fn downgrade(self) -> SharedFrames {
    self.refcount.count.store(1, Ordering::Relaxed);

    let frame_ref = SharedFrames {
        frame: self.start,
        order: self.order,
    };
    mem::forget(self);

    frame_ref
}
```

因为只是转换，这里同样用了 `mem::forget` 避免自动 Drop

## 自动解引用

这里实现了 `Deref` 和 `DerefMut` trait，从而可以把 `UniqueFrames` 直接当作 `&mut Frame` 或 `&Frame` 用

# 共享引用

共享引用在内核开发早期应该用不到，不过还是留一个坑位在这

```rust

pub struct SharedFrames {
    frame: NonNull<Frame>,
    _order: FrameOrder,
}
```

## 安全的创建

每次创建的时候都通过调用 `FrameRc::acquire` 增加引用计数

```rust
pub fn new(frame: NonNull<Frame>) -> Option<Self> {
    unsafe { frame.as_ref().refcount.acquire()? };
    Some(SharedFrames {
        frame,
        _order: FrameOrder::new(0),
    })
}
```

## 不安全的创建

和 `UniqueFrames` 一样，也会碰到传递给 C 的时候，所以也需要一个不安全的创建方式不影响引用计数

```rust
/// 尝试从指针创建 `SharedFrames` 而不增加引用计数
///
/// # Safety
///
/// 调用者必须确保该帧当前没有其他引用，否则可能导致数据竞争、释放后使用等未定义行为
pub unsafe fn from_raw(frame: NonNull<Frame>, order: FrameOrder) -> Option<Self> {
    let refcount = unsafe { frame.as_ref().refcount.get() };
    if refcount != FrameRc::EXCLUSIVE {
        Some(SharedFrames {
            frame,
            _order: order,
        })
    } else {
        None
    }
}
```

# 释放

共享引用和独占引用的释放逻辑大致上一样，所以使用同一个函数

```rust
impl Drop for SharedFrames {
    fn drop(&mut self) {
        auto_free(self.frame);
    }
}

impl Drop for UniqueFrames {
    fn drop(&mut self) {
        auto_free(self.start);
    }
}
```

首先是外层：

```rust
fn auto_free(mut frame: NonNull<Frame>) {
    // 释放引用计数
    let frame_ref = unsafe { frame.as_ref() };
    let count = frame_ref.refcount.release();
    if let FrameTag::Anonymous = frame_ref.get_tag() {
        // 如果还有映射存在，不立即释放帧资源，等待最后一个映射解除时再释放
        if unsafe { frame_ref.get_data().anonymous.release() } > 0 {
            return;
        }
    }

    match count {
        Some(1) => {
            frame_ref
                .refcount
                .count
                .store(FrameRc::EXCLUSIVE, Ordering::Relaxed);
            last_free(unsafe { frame.as_mut() });
        }
        Some(0) if let FrameTag::Buddy = frame_ref.get_tag() => {
            last_free(unsafe { frame.as_mut() });
        }
        Some(_) => {
            // 还有其他引用存在，不释放
        }
        None => {
            // double-free 或引用计数已经为 0
            // 记录错误但不 panic
            printk!(
                "ERROR: double-free detected for frame {}, addr = {:#x} (memory leaked)",
                unsafe { frame.as_ref().to_frame_number() },
                frame.addr()
            );
        }
    }
}
```

分别判断了两种引用是否不存在其他引用（对于独占引用来说是一定的），然后更新计数并调用释放函数：

```rust
fn last_free(frame: &mut Frame) {
    // 最后一个引用被释放——归还给分配器
    let frame_number = frame.to_frame_number();

    if let Err(e) = FRAME_MANAGER.deallocate(frame) {
        // 不 panic，仅记录错误。
        // 内存会泄漏，但系统继续运行。
        // 比 panic 导致整个系统停机要好。
        printk!(
            "ERROR: failed to free frame {}: {:?} (memory leaked)",
            frame_number,
            e
        );
    }
}
```

