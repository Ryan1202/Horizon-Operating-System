由于中断的触发方式多种多样，其相应的处理流程也会有不同，所以定义了 `Flow` 来区分

# Flow

## 电平触发 ( Level )

电平触发型中断会在触发中断时将电平修改到 0 或者 1，具体取决于中断控制器将哪种电平看作激活。CPU 在处理完之后需要将电平恢复到原来的状态才能通知设备中断处理完成

```
设备：
        _____________
_______|             |_______
        IRQ asserted
控制器看到 IRQ line 一直处于 active 状态。
```

同样的，在处理过程中如果电平还是保持 active 状态，中断控制器可能会不断的发送中断给 CPU ，所以需要先屏蔽中断再向控制器发送 ack 表示已经收到中断。在处理中断的过程中需要由驱动告诉设备清除中断，然后才能交由框架解除中断的屏蔽

## 边沿触发 ( Edge )

对于边沿触发型中断，电平发生变化才意味着中断的触发，当然更细一点有分上升沿触发和下降沿触发。

```
          ↑ edge
__________|‾‾‾‾‾‾
```

此时中断控制器可以区分当前中断是否已经发送给 CPU，所以不需要屏蔽，不过此时中断控制器会进入等待状态， CPU 还是要发送 ack 表示确认收到从而允许接受下一个中断

## EdgeEOI

和边沿触发几乎相同，只是需要在处理完中断之后发送一个 EOI 信息

## FastEOI

FastEOI 简单很多，这种情况中断控制器已经完成了很多工作，所以 CPU 只需要在中断处理完成之后发送一个 EOI 信息通知就好了

## PerCpu

PerCpu 是属于特定 CPU 的中断，不过我认为这种情况基本是像 Local APIC timer 这种本地设备，暂时就不纳入 IRQ 框架管理了

## Simple

还有种更简单的中断，目前我知道的只有 APIC 的伪中断，这种情况更像是设备只是 “通知” 一下 CPU 发生了这么回事，并不需要回应，所以直接执行中断处理程序即可

# Dispatch


第一步，先检查当前 IRQ 是否已启用， 然后增加计数

```rust
let action = {
    let state = descriptor.state.lock_irqsave();
    if state.status != Status::Enabled {
        drop(state);
        skip(descriptor);
        return None;
    }

    let action = unsafe { descriptor.actions.load(Ordering::Acquire).as_ref() }
        .expect("active IRQ without action");

    begin(descriptor);

    let running = descriptor.in_progress.fetch_add(1, Ordering::Relaxed);
    if running > 0 {
        return None;
    }

    action
};
```




为了能够处理叠加的多个中断，使用了 `loop` 循环

```rust
let mut result = None;
loop {
    let mut current = Some(action);
    while let Some(action) = current {
        if action.enabled.load(Ordering::Relaxed)
            && action.handler.handle(descriptor.irq).is_some()
        {
            result = Some(());
        }
        current = action.next.map(|ptr| unsafe { ptr.as_ref() });
    }
    {
        let state = descriptor.state.lock();
        end(descriptor, matches!(state.status, Status::Enabled));
    }

    let running = descriptor.in_progress.fetch_sub(1, Ordering::Relaxed);
    if running > 1 {
        continue;
    }

    return result;
}
```

检查 `running > 1`。例如原值为 2，减一后仍有一轮待处理；原值为 1，减一归零后退出。

每一轮都会调用 `end()`，包括 `FastEoi` / `EdgeEoi` 的 EOI。循环持续使用最初的 action 头指针，执行期间新头插的共享 action 不会加入这次循环，后续新的执行者才会看到它。

`Stopping` 阻止新的 action 执行者进入，已有执行者继续消费计数。当前循环不会因为状态变成 Disabled 或 Stopping 而直接退出；它仍逐个检查 action 的 `enabled`。`Level` 的结束路径会重新读取状态，只有 Enabled 才解除屏蔽。

`drain()` 等待 `in_progress` 归零，所以覆盖计数内的 action 遍历和每轮 `end()`，不统计 `skip()` 路径。它不能单独证明所有迟到硬件事件都已排空，最后一次注销还要调用 domain 的 `synchronize()`，而普通 LAPIC 停用保留 vector。
