"affinity" 被翻译作 “亲和性”，在多核系统中实际上就是一个位图，用来表示是否屏蔽某个核心。虽然目前还没有实现 SMP 支持，但为了之后省事，还是直接把目前名义上的 `IrqAffinity` 实则单纯是个占位类型的相关机制实现了吧

# CpuMask

这个很重要，不仅在 IRQ 负载均衡里用到，进程 / 线程的调度中也会用于负载均衡或是绑定核心之类的操作

`CpuMask` 基于现有的 `BitSet` 类型，同时还有一个 `ThinCpuMask` 扔掉了 `BitSet` 的包装，只在使用时临时转换成 `BitSet` ，降低了对齐对结构体大小的影响

```rust
const CPU_MASK_COUNTS: usize = (NR_CPUS_MAX / usize::BITS) as usize;
type Static = [usize; CPU_MASK_COUNTS];

#[derive(Clone)]
pub struct ThinCpuMask {
    bits: Static,
}

#[derive(Clone)]
pub struct CpuMask {
    bits: BitSet<Static>,
}
```

比如当这里的 `NR_CPUS_MAX` 为 `64` 时，`CpuMask` 需要 16 字节，而 `ThinCpuMask` 只需 8 字节；而当 `NR_CPUS_MAX` 为 256 时，`CpuMask` 需要 40 字节，`ThinCpuMask` 需要 32 字节。

 `ThinCpuMask` 对于 `NR_CPUS_MAX` 为 2 的幂时，对于 SLUB 通用对象池的利用也会更好一点。

两种类型也实现了互相转换

```rust
impl From<ThinCpuMask> for CpuMask {
    fn from(thin: ThinCpuMask) -> Self {
        Self {
            bits: BitSet::from_storage(thin.bits, NR_CPUS_MAX as usize),
        }
    }
}

impl From<&ThinCpuMask> for CpuMask {
    fn from(thin: &ThinCpuMask) -> Self {
        Self {
            bits: BitSet::from_storage(thin.bits, NR_CPUS_MAX as usize),
        }
    }
}

impl From<CpuMask> for ThinCpuMask {
    fn from(mask: CpuMask) -> Self {
        Self {
            bits: mask.bits.breakdown().0,
        }
    }
}
```

其他的还有一些常用的功能只是 `BitSet` 的封装，不展开了：

- `new`
  - `new_zeroed`
  - `new_full`
- `contains`
- `set`
- `clear`
- `intersect`

# Affinity

对于描述一个 IRQ 框架中的亲和性，按照我的思路需要两个 `mask` ：一个负责描述提交的请求，作为多核负载均衡时的候选列表；另一个则是实际生效的核心，即 IRQ 会被同时送到这些核心

```rust
pub type Mask = ThinCpuMask;

pub struct Affinity {
    pub(super) requested: Mask,
    pub(super) effective: Mask,
}

const impl Default for Affinity {
    fn default() -> Self {
        let requested = Mask::new_full().expect("Failed to create requested affinity mask");
        let effective = Mask::new_zeroed().expect("Failed to create effective affinity mask");
        Self {
            requested,
            effective,
        }
    }
}

impl Affinity {
    pub const fn new(requested: Mask) -> Self {
        Self {
            requested,
            effective: Mask::new_zeroed().expect("Failed to create effective affinity mask"),
        }
    }

    pub const fn requested(&self) -> &Mask {
        &self.requested
    }

    pub const fn effective(&self) -> &Mask {
        &self.effective
    }
}
```

# 与框架结合

数据结构本身其实非常简单，难的是要放到 IRQ 框架中去协作

## 框架侧

### 激活&释放

首先，默认情况下驱动和内核不应该需要操心亲和性，它应该是被动的、自动化完成的。所以可以看到在前面的 `Affinity::default()` 里 `requested` 默认是全为 1，即所有核心都被允许。而且在调用 `request_irq` 时也不需要专门传入参数。

唯一的特别之处在于，框架内部定义的 `Domain` trait 的 `activate` 需要一个 `affinity` 参数。在 `request_irq` 中传入的值正常情况下是在构建 `IrqDescriptor` 的 `state` 时自动创建的默认值，当然也可以在 `request_irq` 之前通过 `set_affinity` 来修改，不过普通的驱动在调用 `request_irq` 之前是获取不到 `IrqDescriptor` 的。

```rust
pub(super) struct State {
    /// 当前的状态
    pub(super) status: Status,
    /// CPU 亲和性
    pub(super) affinity: Affinity,
    /// 已注册且可用的 handler 数量
    pub(super) active: usize,
}
```

所以 `request_irq` 适当的进行了改动

```rust
// 解锁前修改状态为 Stopping 防止其他 `request_irq` 修改
state.status = Status::Stopping;

// action 必须先发布，activate 的最终提交可能立即允许硬件开始投递。
let affinity = state.affinity.requested().clone();
drop(state);

// SAFETY: 已检查 active 计数确认未激活
let effective = unsafe { domain::activate(irq, &descriptor.data, &(&affinity).into()) }
    .inspect_err(|_| {
        let mut state = descriptor.state.lock_irqsave();
        let removed = descriptor.actions.swap(null_mut(), Ordering::AcqRel);

        assert_eq!(removed, action.as_ptr());
        assert_eq!(state.active, 1);

        state.active = 0;
        state.status = Status::Inactive;
        drop(state);

        // SAFETY: 激活失败后没有 handle，且 Stopping 阻止了 action 遍历。
        let _ = unsafe { Box::<_, Kmalloc>::from_non_null_in(action, Kmalloc::default()) };
    })?;

let mut state = descriptor.state.lock_irqsave();
state.affinity.effective = effective.into();

state.status = Status::Disabled;
drop(state);
```

在 `activate` 之前需要解锁 `state` ，所以需要提前把 `requested` 复制出来，`effective` 通过返回值传递回来，在重新加锁后更新 `effective`

`Domain` 也进行了更新，增加了更清晰的限制，层级间的调用也从框架调用变成了驱动自行决定

```rust
/// 激活 IRQ，返回实际生效的 CPU 集合
///
/// # Safety
///
/// 不允许在已激活的 IRQ 上调用 activate
unsafe fn activate(
    &self,
    irq: IrqNumber,
    data: &IrqData,
    affinity: &CpuMask,
) -> Result<CpuMask, IrqError>;
```

```rust
/// 激活 IRQ，返回实际生效的 CPU 集合
///
/// # Safety
///
/// 不允许在已激活的 IRQ 上调用 activate
pub(super) unsafe fn activate(
    irq: IrqNumber,
    data: &IrqData,
    affinity: &CpuMask,
) -> Result<CpuMask, IrqError> {
    // SAFETY: 由调用者保证 irq 未激活
    unsafe { data.domain().activate(irq, data, affinity) }
}
```

`deactivate` 也是同理

```rust
/// 释放路由
///
/// # Safety
///
/// 调用前需确保已关闭并屏蔽该 IRQ，且所有旧的执行者已退出
unsafe fn deactivate(&self, data: &IrqData) -> Result<(), IrqError>;
```

```rust
/// 释放路由
///
/// # Safety
///
/// 调用前需确保已关闭并屏蔽该 IRQ，且所有旧的执行者已退出
pub(super) unsafe fn deactivate(data: &IrqData) -> Result<(), IrqError> {
    // SAFETY: 由调用者保证
    unsafe { data.domain().deactivate(data) }
}
```

### 更新

亲和性是免不了要在运行中进行更新的，不管是负载均衡需要修改的 `effective` 还是可能由用户 / 应用修改的 `requeseted` ，所以 `Domain` 新增了一个 `update_affinity` 函数

```rust
/// 更新亲和性，返回实际生效的 CPU 集合
///
/// # Safety
///
/// 只能在已激活的 IRQ 上调用 update_affinity
unsafe fn update_affinity(
    &self,
    irq: IrqNumber,
    data: &IrqData,
    affinity: &CpuMask,
) -> Result<CpuMask, IrqError>;
```

### 回收

在运行时更新需要一小段时间同时存在新旧两个路由，即新的 IRQ 发给新的路由，旧的路由处理还没处理完的遗留中断。而且由于需要访问 APIC 的处理状态，而 Local APIC 每个核心只能访问到属于自己的那份，也必须把旧的路由交给原来的 CPU 核心来释放

```rust
/// 回收旧的路由
fn try_reclaim_route(&self, data: &IrqData) -> Result<(), IrqError>;
```

为了避免一直运行一个后台服务来处理这一操作且不阻塞中断，在存在尚未回收的路由时，让每个 CPU 在每次发生时钟中断时尝试回收或者发送 IPI

## 驱动侧

除了同步之外，核心的工作其实都在 Local APIC 的部分，I/O APIC 基本上只是起到一个转发的作用

### 同步

首先 I/O APIC 和 Local APIC 都添加了 `synchronize()` 函数用来等待 IRR / Remote IRR 清零 （即中断处理完成），这是切换亲和性时必要的步骤

I/O APIC:

```rust
fn synchronize(&self, data: &IrqData) {
    let chip = data
        .chip()
        .downcast_ref::<IoApic>()
        .expect("IOAPIC chip mismatch");
    let info = data.chip_data::<Info>();

    if info.trigger_mode == TriggerMode::Level {
        chip.synchronize(info.pin);
    }

    if let Some(parent) = data.parent() {
        parent.domain().synchronize(parent);
    }
}
```

Local APIC:

```rust
/// 读取 ISR 和 IRR 检查当前中断向量是否正忙
pub(crate) fn is_busy(&self, vector: u8) -> bool {
    let reg = self.reg();
    let isr = reg.read_common(Isr(vector));
    let irr = reg.read_common(Irr(vector));

    let mask = 1 << (vector & 0x1f);

    isr & mask != 0 || irr & mask != 0
}
```

```rust
fn synchronize(&self, data: &IrqData) {
    let local = data.chip_data::<LocalApicData>();
    let vector = local
        .route
        .lock_irqsave()
        .current
        .map(|r| r.vector)
        .unwrap_or(0);
    let lapic = LocalApic::get();

    while lapic.is_busy(vector) {
        spin_loop();
    }
}
```

### 更新

更新时，需要先检查旧路由是否已经释放，如果没有则直接返回 `Busy` 。

然后就是正常的分配新的 `vector`，再把原来的路由放进 `old` 字段

```rust
unsafe fn update_affinity(
    &self,
    irq: IrqNumber,
    data: &IrqData,
    affinity: &CpuMask,
) -> Result<CpuMask, IrqError> {
    let local = data.chip_data::<LocalApicData>();
    let mut effective = CpuMask::new_zeroed().ok_or(MemoryError::OutOfMemory)?;

    let mut route = local.route.lock_irqsave();

    if route.old.is_some() {
        return Err(IrqError::Busy);
    }

    let allocated = VectorManager::get().allocate(irq, affinity, local.scope)?;
    effective.set(allocated.cpu);

    // 旧的路由需要等待目标 CPU 完成所有已发起的事件后才能释放
    route.old = route.current.replace(allocated);

    Ok(effective)
}
```

对于 I/O APIC 来说，就是预先屏蔽 IRQ ，然后转发调用到上一层（Local APIC），拿到结果之后再更新重定向条目并恢复屏蔽状态

需要注意的是，I/O APIC 的 Remote IRR 字段只在中断的触发类型是 `Level` 时才有效，相应的 `sychronize()` 也是

```rust
unsafe fn update_affinity(
    &self,
    irq: IrqNumber,
    data: &IrqData,
    affinity: &CpuMask,
) -> Result<CpuMask, IrqError> {
    let info = data.chip_data::<Info>();
    let parent = data.parent().ok_or(IrqError::NotFound)?;

    let chip = data
        .chip()
        .downcast_ref::<IoApic>()
        .expect("IOAPIC domain chip type mismatch");

    let masked = chip.regs.lock_irqsave().set_mask(info.pin, true);

    // 先分配新的路由
    // SAFETY: 由调用者保证 irq 已激活
    let mask = unsafe { parent.domain().update_affinity(irq, parent, affinity) };
    let route = LocalApicDomain::route(parent).ok_or(IrqError::NotFound)?;

    let regs = chip.regs.lock_irqsave();

    let mask = mask.inspect_err(|_| {
        regs.set_mask(info.pin, masked);
    })?;

    if info.trigger_mode == TriggerMode::Level {
        chip.synchronize(info.pin);
    }

    // SAFETY: 已屏蔽中断并确认在 Level 模式下 Remote IRR 已清零
    unsafe {
        regs.set_redirection_entry(
            info.pin,
            RedirectionEntry::new(
                route.vector,
                route.apic_id,
                info.trigger_mode,
                info.polarity,
            ),
        )
    };

    // 重新设置屏蔽状态，返回旧状态
    regs.set_mask(info.pin, masked);

    Ok(mask)
}
```

### 激活&释放

对于 **Local APIC**，`activate` 的变化主要在于对 `Affinity` 的处理

**在激活时**，还需要特地考虑旧路由，因为可能是在 `deactivate` 之后又重新 `activate` 了，此时有可能旧的路由还没处理完，这种情况是允许的，但 `current` 必须为 `None`

分配完了还是正常的更新 `effective`

```rust
let local = data.chip_data::<LocalApicData>();

let mut effective = CpuMask::new_zeroed().ok_or(MemoryError::OutOfMemory)?;
let mut route = local.route.lock_irqsave();

assert!(route.current.is_none());
if route.old.is_some() {
    drop(route);
    let _ = self.try_reclaim_route(data);
    route = local.route.lock_irqsave();
}

let allocated = VectorManager::get().allocate(irq, affinity, local.scope)?;
route.current = Some(allocated);

effective.set(allocated.cpu);
```

**在释放时**，同样也需要考虑到旧路由还没释放的情况，此时需要返回错误

```rust
unsafe fn deactivate(&self, data: &IrqData) -> Result<(), IrqError> {
    let mut route = data.chip_data::<LocalApicData>().route.lock_irqsave();
    if route.old.is_some() {
        return Err(IrqError::Busy);
    }

    route.old = route.current.take();

    Ok(())
}
```

对于 **I/O APIC** ，主要的变化就是在于加入了 `sychronize()`

如 `activate` ，由于 `synchronize` 也有加锁，所以需要临时解锁避免死锁

```rust
regs.active
    .try_set(info.pin as usize)
    .ok_or(IrqError::Busy)?;
drop(regs);

if info.trigger_mode == TriggerMode::Level {
    chip.synchronize(info.pin);
}

// SAFETY: `activate` 确保当前未激活，在未激活状态下处于已屏蔽状态，并已手动同步
unsafe {
    chip.regs.lock_irqsave().set_redirection_entry(
        info.pin,
        RedirectionEntry::new(
            route.vector,
            route.apic_id,
            info.trigger_mode,
            info.polarity,
        ),
    )
};
```

`deactivate` 仅仅是增加了调用父层的 `deactivate`

### 回收

按目前的设计，回收依赖于时钟中断或者 IPI ，所以目前只是留一个接口等待未来接入

I/O APIC 层什么都不用做

```rust
fn try_reclaim_route(&self, data: &IrqData) -> Result<(), IrqError> {
    let parent = data.parent().expect("IOAPIC without LAPIC parent");
    parent.domain().try_reclaim_route(parent)
}
```

Local APIC 则需要确认是否是该 CPU 的中断向量以及是否处理完毕，都过了才能释放

```rust
fn try_reclaim_route(&self, data: &IrqData) -> Result<(), IrqError> {
    let local = data.chip_data::<LocalApicData>();

    let mut route = local.route.lock_irqsave();

    if let Some(old) = &route.old {
        let lapic = LocalApic::get();
        if old.apic_id != lapic.id() {
            return Err(IrqError::NotFound);
        }
        if lapic.is_busy(old.vector) {
            // 如果旧的向量还在忙，不能立即释放
            return Err(IrqError::Busy);
        }

        VectorManager::get().free(unsafe { route.old.take().unwrap_unchecked() });
    }

    Ok(())
}
```

