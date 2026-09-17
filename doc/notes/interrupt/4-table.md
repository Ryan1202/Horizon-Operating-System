# Table

既然把中断号分开成了 CPU 侧最终见到的 vector 和全局的 irq ，就需要单独的 irq 表来管理 descriptor。

```rust
pub static IRQ_DESCRIPTORS: IrqTable = IrqTable::new();

type Descriptors = Spinlock<[Option<Arc<IrqDescriptor, Kmalloc>>; MAX_IRQS]>;
pub struct IrqTable(Descriptors);
```

由于 irq 号的范围比较大，所以是通过动态分配 + 预留指针数组的形式进行管理。为了解决 Use After Free 的问题（比如在中断路径刚获取到 `IrqDescriptor` 的引用就在另一个核心上释放了内存，这是 Rust 无法管到的情况），使用了 `Arc` 来管理内存的生命周期，等未来支持了 RCU 可能使用 RCU 会更好。

```rust
pub fn lookup(&self, irq: IrqNumber) -> Option<Arc<IrqDescriptor, Kmalloc>> {
    self.0.lock_irqsave()[irq.get()].clone()
}
```

## 初始化

初始状态就是将所有的表项都设为 `None` 

```rust
const fn new() -> Self {
    Self(Spinlock::new([const { None }; MAX_IRQS]))
}
```

## 分配

`IrqDescriptor` 先构造再发布。当前 x86 启动路径为 ISA 预留 IRQ 0–15；PCI INTx 则通过 ACPI `_PRT` 路由来预留

发布的步骤非常简单，先将 `IrqDescriptor` 移到堆上，然后获取锁来修改全局的指针数组

```rust
pub fn publish(&self, descriptor: IrqDescriptor) -> Result<(), IrqError> {
    let irq = descriptor.irq.get();
    let descriptor = Arc::new_in(descriptor, Kmalloc::default());
    let mut table = self.0.lock_irqsave();

    if table[irq].is_some() {
        // descriptor 在锁之前声明；返回时先解锁，再析构
        return Err(IrqError::AlreadyPublished);
    }

    table[irq] = Some(descriptor);

    Ok(())
}
```

### ISA

ISA IRQ 有特殊处理，在启动时直接将 IRQ 0-15 预留好，等后续驱动请求时再修改

```rust
/// 启动阶段发布具有占位内容的 descriptor，不分配 IOAPIC/LAPIC mapping
pub(super) fn init_irqs() -> Result<(), IrqError> {
    let irqs = IrqReservation::reserve((0..IRQ_COUNT).into())?;

    for irq in irqs.iter() {
        IRQ_DESCRIPTORS.publish(IrqDescriptor::placeholder(irq))?;
        VectorManager::get()
            .reserve(irq, irq.get() as u8 + 0x20)
            .inspect_err(|e| {
                printk!(
                    "failed to reserve vector for ISA IRQ {}: {:?}\n",
                    irq.get(),
                    e
                );
            })?;
    }

    *ISA_NUMBERS.lock_irqsave() = Some(irqs);

    Ok(())
}
```

为后续请求时准备了一个接口 `realloc`

```rust
pub fn realloc(
    &self,
    irq: IrqNumber,
    sharing: IrqSharing,
    domain: &'static dyn Domain,
    flow: Flow,
    arg: &dyn Any,
) -> Result<(), IrqError> {
    // 分配可能等待，构造完成后再取得表锁并提交
    let data = domain.allocate(irq, arg)?;

    // descriptor 始终留在表中。若恰好有一次占位 lookup 持有 Arc，释放表锁等它退出后重试
    // 不制造 vector 已映射而 descriptor 暂时缺失的窗口
    let _ = loop {
        let mut table = self.0.lock_irqsave();
        let descriptor = table[irq.get()].as_mut().ok_or(IrqError::NotFound)?;

        if descriptor.is_configured() {
            return Err(IrqError::Busy);
        }

        if let Some(descriptor) = Arc::get_mut(descriptor) {
            break descriptor.realloc(sharing, data, flow);
        }

        drop(table);
        spin_loop();
    };

    // 旧 Placeholder 的析构可能释放内存，不放在 descriptor 表锁内执行。

    Ok(())
}
```

# Descriptor

`IrqDescriptor` 负责具体描述每个 IRQ 的信息

```rust
pub struct IrqDescriptor {
    /// 全局 IRQ 编号
    pub irq: IrqNumber,
    /// IRQ 的数据，每个中断 chip 维护一份自己的数据
    pub data: IrqData,
    /// IRQ 的处理流程
    pub flow: Flow,
    /// 已注册的 handler 链表头指针
    pub(super) actions: AtomicPtr<IrqAction>,
    /// 当前的状态
    pub(super) state: Spinlock<State>,
    /// 共享/独占
    pub(super) sharing: IrqSharing,
    /// 当前正在执行的 handler 数量
    pub(super) in_progress: AtomicUsize,
}
```

`irq` 是全局中断号，`data` 是跟硬件相关的信息

`flow` 则是用于区分中断的处理流程，比如发送 EOI 的时机等等（比如 APIC 会有伪中断不应该发送 EOI）

`action` 是一个单向链表，保存所有的设备中断处理程序，由于一般不会有非常多中断共享同一个 IRQ ，所以使用单向链表并只随 `IrqDescriptor` 一起释放

`state` 则是中断的状态信息，使用 `Spinlock` 提供内部可变性

`sharing` 则是用于区分共享中断和独占中断，两种中断只在注册和注销时处理方式会有差别

`in_progress` 记录了正在运行的处理程序计数，在屏蔽并关闭硬件中断前需要先依赖这一计数等待已开始的处理程序结束

## IrqData

`IrqData` 负责保存跟中断控制器硬件相关的信息，通过 `parent` 指针来从最靠近设备的一端一直连接到最靠近 CPU 的一端

```rust
/// 发布后拓扑不可变。free 在本层字段及 parent 析构之前运行。
pub struct IrqData {
    hwirq: RawIrq,
    domain: &'static dyn Domain,
    chip: Arc<dyn IrqChip, Kmalloc>,
    chip_data: Box<dyn Any + Send + Sync, Kmalloc>,

    parent: Option<Box<IrqData, Kmalloc>>,
}
```

硬件 IRQ 使用了 `RawIrq` 而不是 `HardwareIrq` 因为可以通过类型擦除保证作为通用数据结构可以被不知道 IRQ Domain 的情况下使用，`domain` 也是因此采用了 `&dyn Domain` 动态分发（为了避免生命周期问题使用了 `'static` 生命周期，因此需要确保 `Domain` 的生命周期长于 `IrqDescriptor` ，大多数的 irq domain 可以直接定义为全局变量 ）

`chip` 则是由于需要被多个中断引用，所以才用了 `Arc` ，`chip_data` 是每一个中断在每一级中断控制器的私有数据，使用独占的 `Box` ，`parent` 用于连接多层的中断层级（比如最基础的从 I/O APIC 到 Local APIC）

最外层的 `IrqData` 是直接嵌入进 `IrqDescriptor` 中的，可以减少一次额外的内存分配，而且一般情况下 `IrqData` 的层级也不会太多

`IrqData` 还有一个用来分配并初始化 `parent` 的辅助函数

```rust
pub fn alloc_parent(
    &mut self,
    irq: IrqNumber,
    domain: &'static dyn Domain,
    arg: &dyn Any,
) -> Result<&mut IrqData, super::IrqError> {
    let current = {
        let mut current = self;
        while current.parent.is_some() {
            current = unsafe { current.parent.as_deref_mut().unwrap_unchecked() };
        }
        current
    };

    let parent = Box::try_new_in(domain.allocate(irq, arg)?, Kmalloc::default())
        .map_err(|_| MemoryError::OutOfMemory)?;

    current.parent = Some(parent);

    Ok(current.parent.as_deref_mut().unwrap())
}
```

同时还实现了 `Drop` ，哪怕在分配 `parent` 时失败也能自动为临时的 `IrqData` 从 domain 释放

```rust
impl Drop for IrqData {
    fn drop(&mut self) {
        self.domain.free(self);
    }
}
```

## State

还有就是状态信息了

```rust
#[derive(Clone, Copy, PartialEq, Eq)]
pub(super) enum Status {
    /// 尚未激活
    Inactive,
    /// 已激活，但未启用任何 handler
    Disabled,
    /// 已激活，至少有一个 handler 已启用
    Enabled,
    /// 正在停用，禁止新的 handler 执行者进入，正在等待所有旧执行者退出
    Stopping,
}

pub(super) struct State {
    /// 当前的状态
    pub(super) status: Status,
    /// 已注册且可用的 handler 数量
    pub(super) active: usize,
}
```

## IrqAction

`IrqAction` 用来描述一个中断处理程序，除此之外还包含一个链表指针和是否已启用的标志

```rust
pub struct IrqAction {
    pub(super) next: Option<NonNull<IrqAction>>,
    pub(super) handler: Arc<dyn IrqHandler, Kmalloc>,
    pub(super) enabled: AtomicBool,
}
```

`IrqHandler` 就是一个描述中断处理程序的 `trait`

```rust
pub trait IrqHandler: Send + Sync {
    fn handle(&self, irq: IrqNumber) -> Option<()>;
}
```

## 撤销发布

撤销发布时，会等到其他的引用都结束了，然后才将 `IrqDescriptor` 交给调用者

```rust
pub fn unpublish(&self, irq: IrqNumber) -> Result<Arc<IrqDescriptor, Kmalloc>, IrqError> {
    let descriptor = self.0.lock_irqsave()[irq.get()].take();
    if let Some(mut descriptor) = descriptor {
        loop {
            if let Some(_) = Arc::get_mut(&mut descriptor) {
                return Ok(descriptor);
            }
            spin_loop();
        }
    }
    Err(IrqError::NotFound)
}
```
