# Handle

驱动程序不应该直接持有 `IrqDescriptor` 的引用，所以我还专门设计了一个 `IrqHandle` 顺便处理共享 IRQ 的生命周期问题

对于具有多个 `action` 的情况，到 `IrqDescriptor` 里查找虽然在大多数情况下也不会有很大性能影响，但还是不太方便，所以 `IrqHandle` 同时持有 `IrqDescriptor` 的共享引用和自己注册的 handler 的 `IrqAction` 指针

```rust
#[must_use = "dropping the IRQ handle unregisters its handler"]
pub struct IrqHandle {
    descriptor: Arc<IrqDescriptor, Kmalloc>,
    action: NonNull<IrqAction>,
}

// SAFETY: 管理方法独占 handle；状态锁和 Stopping 排空协议保护链表，handler 是 Send + Sync
unsafe impl Send for IrqHandle {}
unsafe impl Sync for IrqHandle {}
```

## 获取 handle (`request_irq`)

通过 `request_irq` 来请求 IRQ ，成功时会返回一个 `IrqHandle` 负责管理生命周期和提供一些实用功能

在一开始，需要先检查一下是否能够请求

```rust
let descriptor = loop {
    let descriptor = IRQ_DESCRIPTORS.lookup(irq).ok_or(IrqError::NotFound)?;
    if descriptor.is_configured() {
        break descriptor;
    }
    let Some((domain, flow, arg)) = realloc else {
        return Err(IrqError::NotFound);
    };
    drop(descriptor);
    match IRQ_DESCRIPTORS.realloc(irq, sharing, domain, flow, arg) {
        Ok(_) | Err(IrqError::Busy) => {}
        Err(error) => {
            return Err(error);
        }
    }
};
```

`Busy` 表示其他请求已经完成了占位配置，重新查询即可。是否允许注册由稍后的 sharing 和 `active` 检查决定；已配置的独占 descriptor 在没有 handler 时仍可重新请求。

然后先为 action 分配内存并初始化

```rust
let mut action = Box::<_, Kmalloc>::new_in(
    IrqAction {
        next: None,
        handler,
        enabled: AtomicBool::new(false),
    },
    Kmalloc::default(),
);
```

接下来等待激活或注销事务退出 `Stopping`，取得状态锁后检查 sharing。此处尚不修改状态。

```rust
let mut state = loop {
    let state = descriptor.state.lock_irqsave();
    if state.status != Status::Stopping {
        break state;
    }

    drop(state);
    spin_loop();
};
```

首个 action 必须匹配 mapping 的 sharing；已有 action 时，双方都必须为 `Shared`。后续共享注册直接头插新节点并返回，保持原来的 `Enabled` / `Disabled` 状态。

```rust
if state.active == 0 {
    if descriptor.sharing != sharing {
        return Err(IrqError::ExclusiveViolation);
    }
} else if descriptor.sharing != IrqSharing::Shared || sharing != IrqSharing::Shared {
    return Err(IrqError::ExclusiveViolation);
} else {
    let head = NonNull::new(descriptor.actions.load(Ordering::Relaxed))
        .expect("registered IRQ without an action");

    // 管理者由 state 锁串行化，旧节点的 next 不变，因此正在进行的遍历可以继续使用旧 head，
    // 新遍历则从完整初始化的新节点开始
    action.next = Some(head);

    let action = Box::into_non_null_with_allocator(action).0;

    descriptor.actions.store(action.as_ptr(), Ordering::Release);

    state.active += 1;
    drop(state);

    return Ok(IrqHandle::new(descriptor, action));
}
```

先配置基础信息

```rust
state.active = 1;
```

只有首个 action 需要激活 domain：先发布 action，再在解锁前将状态改为 `Stopping`，阻止其他注册者和新的 handler 遍历进入。

```rust
assert!(descriptor.actions.load(Ordering::Relaxed).is_null());
let action = Box::into_non_null_with_allocator(action).0;
descriptor.actions.store(action.as_ptr(), Ordering::Release);
state.status = Status::Stopping;
drop(state);
```

尝试激活，如果失败需要回滚

```rust
// action 必须先发布，activate 的最终提交可能立即允许硬件开始投递。
let mut affinity = Affinity::Auto;
if let Err(error) = domain::activate(irq, &descriptor.data, &mut affinity) {
    let mut state = descriptor.state.lock_irqsave();
    let removed = descriptor.actions.swap(null_mut(), Ordering::AcqRel);

    assert_eq!(removed, action.as_ptr());
    assert_eq!(state.active, 1);

    state.active = 0;
    state.status = Status::Inactive;
    drop(state);

    // SAFETY: 激活失败后没有 handle，且 Stopping 阻止了 action 遍历。
    let _ = unsafe { Box::<_, Kmalloc>::from_non_null_in(action, Kmalloc::default()) };

    return Err(error);
}
```

如果成功则更新状态并返回 handle

```rust
descriptor.state.lock_irqsave().status = Status::Disabled;

Ok(IrqHandle::new(descriptor, action))
```

## 释放

释放先执行管理上下文检查，再调用 `disable_irq()`。这不仅清除当前 action 的 `enabled`，还会在没有启用的 action 时屏蔽硬件源。

```rust
assert_management();
self.disable_irq();
```

减少 `active` 后，如果还有其他已注册 handler，就保留当前禁用节点并直接返回；最后一个 handle 才将状态切换到 `Stopping`。

```rust
let descriptor = self.descriptor();
{
    let mut state = descriptor.state.lock_irqsave();
    assert!(state.active > 0, "IRQ action count underflow");

    state.active -= 1;
    if state.active != 0 {
        // 非最后一个节点 disabled 留在链中，避免为普通 hardirq 遍历增加 reader 字段或回收协议
        return;
    }

    assert!(matches!(state.status, Status::Enabled | Status::Disabled));
    state.status = Status::Stopping;
}
```

接下来，先屏蔽该 IRQ ，等待正在执行的 handler 结束

```rust
let data = &descriptor.data;
if descriptor.flow != Flow::Simple {
    data.chip().mask(data);
}

descriptor.drain();
```

在处理结束后，根据 flow 的类型的不同又有可能解除屏蔽，所以需要重新屏蔽

```rust
// 封闭旧 flow 在第一次 mask 前已经进入、并在退出时恢复投递的窗口。
if descriptor.flow != Flow::Simple {
    data.chip().mask(data);
}
```

然后通过中断控制器提供的 `synchronize` 方法确认已经收到响应后，再取消激活该中断

```rust
domain::synchronize(data);
domain::deactivate(data);
```

然后取出 action 链表，将状态设为 `Inactive`

```rust
let actions = {
    let mut state = descriptor.state.lock_irqsave();
    assert!(state.status == Status::Stopping && state.active == 0);

    let actions = descriptor.actions.swap(null_mut(), Ordering::AcqRel);

    state.status = Status::Inactive;
    actions
};
```

最后才将所有 action 的内存释放掉

```rust
// SAFETY: Stopping 已阻止新遍历，完整 flow 已排空，head 也已摘除
unsafe { IrqDescriptor::reclaim_actions(actions) };
```

```rust
/// 释放 action 链表中所有 action 的内存
/// 
/// # Safety
///
/// head 必须已经从 descriptor 摘除，且所有旧遍历都已退出
pub(super) unsafe fn reclaim_actions(head: *mut IrqAction) {
    let mut current = NonNull::new(head);

    while let Some(action) = current {
        // SAFETY: 调用方保证旧 action 链已经没有读者
        current = unsafe { action.as_ref() }.next;

        // SAFETY: 每个节点均由 Box::into_non_null_with_allocator 发布且只回收一次
        let _ = unsafe { Box::<_, Kmalloc>::from_non_null_in(action, Kmalloc::default()) };
    }
}
```

## 启用/关闭 IRQ

对于已经拿到了 handle 的 IRQ，`IrqHandle` 提供了启用/关闭 IRQ 的功能，具体到每个 action 是通过各自的 `enabled` 字段判断是否需要调用。在关闭时如果是独占 IRQ 或者所有 action 都关闭则会使用硬件屏蔽；同样的，在开启时只要任意 IRQ 开启了就会解除硬件屏蔽

```rust
/// 软件开启 IRQ，如果硬件已屏蔽则解除硬件屏蔽
pub fn enable_irq(&mut self) {
    self.set_enabled(true);

    let descriptor = self.descriptor();
    let data = &descriptor.data;
    let mut state = descriptor.state.lock_irqsave();

    if state.status == Status::Disabled {
        // 如果当前已关闭则可以直接开启
        state.status = Status::Enabled;
        if descriptor.flow != Flow::Simple {
            data.chip().unmask(data);
        }
    }
}

/// 软件关闭 IRQ，如果所有 handler 都关闭则硬件屏蔽
pub fn disable_irq(&mut self) {
    self.set_enabled(false);

    let descriptor = self.descriptor();
    let data = &descriptor.data;
    let mut state = descriptor.state.lock_irqsave();

    if state.status == Status::Enabled {
        // 如果当前已开启则需要所有 action 都关闭才能屏蔽
        if descriptor.sharing == IrqSharing::Exclusive {
            state.status = Status::Disabled;
            if descriptor.flow != Flow::Simple {
                data.chip().mask(data);
            }
        } else {
            let mut all_disabled = true;
            let mut current = unsafe { descriptor.actions.load(Ordering::Relaxed).as_ref() };
            while let Some(next) = current {
                if next.enabled.load(Ordering::Acquire) {
                    all_disabled = false;
                    break;
                }
                current = next.next.map(|ptr| unsafe { ptr.as_ref() });
            }

            if all_disabled {
                state.status = Status::Disabled;
                if descriptor.flow != Flow::Simple {
                    data.chip().mask(data);
                }
            }
        }
    }
}

fn set_enabled(&mut self, enabled: bool) {
    assert_management();

    unsafe { self.action.as_ref() }
        .enabled
        .store(enabled, Ordering::Relaxed);
}
```

`enable_irq()` / `disable_irq()` 和 handle 析构需要满足 `assert_management()` 的上下文要求。`request_irq()` 与 table 的当前实现没有同样的入口断言，但包含分配和忙等，也应从允许 IRQ 管理操作的上下文调用。

需要区分关闭、注销与同步：`disable_irq()` 不调用 `drain()`；非最后一个共享 handle 的析构也不等待已经开始的回调。禁用节点及其 handler 的 `Arc` 留在链上，直到最后一个 handle 注销时统一回收。这保证节点内存不会立即释放，但不能保证禁用或非最后一次注销返回后，旧回调已经结束。尤其 C 回调的裸 `arg` 不由这个 `Arc` 管理，不能仅凭这样的返回就释放它。

对应源码：[handle.rs](../../../src/kernel/interrupt/irq/handle.rs)、[descriptor.rs](../../../src/kernel/interrupt/irq/descriptor.rs)、[ffi.rs](../../../src/kernel/interrupt/ffi.rs)。
