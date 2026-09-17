# IRQ 号管理

在 IRQ 框架中，由于来自不同中断控制器的 IRQ 号都是各自编码的而且会重复，需要一个全局唯一的虚拟 IRQ 号来在系统内进行区分

## 硬件 IRQ

对于硬件 IRQ ，还需要区分 IRQ 域。比如 I/O APIC ，会存在多个芯片，但是使用了 GSI 统一编号，所以此时的硬件 IRQ 编号应该按照 I/O APIC 域来区分而不是按 I/O APIC 芯片来区分

```rust
/// 显式擦除 domain 类型后的硬件编号
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RawIrq(u32);
impl RawIrq {
    pub const fn new(number: u32) -> Self {
        Self(number)
    }

    pub const fn get(self) -> u32 {
        self.0
    }
}

pub struct HardwareIrq<D: Domain> {
    number: RawIrq,
    _marker: PhantomData<D>,
}
```

`RawIrq` 是最原始的 IRQ，除了硬件 IRQ 号之外不带任何信息。

在 `RawIrq` 的基础上引入 IRQ 域的标识就是 `HardwareIrq` 了，`Domain` 是一个定义了 IRQ 域需要实现的功能的 trait，使用 `PhantomData` 确保不占用实际空间。

```rust
impl<D: Domain> HardwareIrq<D> {
    pub const fn new(number: RawIrq) -> Self {
        Self {
            number,
            _marker: PhantomData,
        }
    }

    pub const fn get(self) -> u32 {
        self.number.get()
    }

    pub const fn into_raw(self) -> RawIrq {
        self.number
    }
}

impl<D: Sized + Domain> Clone for HardwareIrq<D> {
    fn clone(&self) -> Self {
        *self
    }
}

impl<D: Sized + Domain> Copy for HardwareIrq<D> {}
```

为了方便使用， `HardwareIrq` 允许随意构造、复制

## 全局 IRQ

在 Linux 中，一般使用 virq 的叫法来表示这是一个虚拟的 IRQ。我更偏好直接使用 irq ，并使用 hardware_irq 来区分

所以定义了一个 `IrqNumber` 类型

```rust
/// Core 分配的虚拟编号；复制编号不延长 mapping 生命周期。
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct IrqNumber(pub(super) u32);

pub static INVALID_IRQ: IrqNumber = IrqNumber(MAX_IRQS as u32);

impl IrqNumber {
    pub const fn get(&self) -> usize {
        self.0 as usize
    }
}
```

为了避免随意构造有效的 `IrqNumber`，没有提供 `new` 方法。`INVALID_IRQ` 则是哨兵，用于 vector 反查数组的空项。同时为了方便使用还是实现了 `Clone` 和 `Copy` ，虽然这样又会出现 IRQ 号注销后还拿着 `IrqNumber` 的问题，但本来也不需要它完全解决这个问题

# IRQ 号分配

既然 `IrqNumber` 已经是全局的资源了，自然也需要一个分配器，这里的分配器还是基于位图 `BitSet` 来管理

```rust
pub(super) const MAX_IRQS: usize = 4096;
type IrqBits = BitSet<[usize; MAX_IRQS / usize::BITS as usize]>;

static ALLOCATOR: Spinlock<IrqBits> = Spinlock::new(BitSet::zeroed(MAX_IRQS));
```

目前最大 IRQ 数被设为了 4096 个

通过 `IrqReservation` 来进行分配和自动释放，如果 IRQ 已经被 `IrqDescriptor` 使用则不会跟着自动释放

```rust
/// 预留编号范围，发布后由 descriptor 表占用，未发布的编号在 drop 时归还
pub struct IrqReservation {
    number: Range<usize>,
}

impl IrqReservation {
    pub fn new(count: NonZeroUsize) -> Result<Self, IrqError> {
        if count.get() > MAX_IRQS {
            return Err(IrqError::InvalidArgument);
        }

        let base = ALLOCATOR
            .lock()
            .allocate(0, count, 1)
            .ok_or(IrqError::OutOfIrq)?;

        Ok(Self {
            number: (base..base + count.get()).into(),
        })
    }

    pub fn reserve(range: Range<usize>) -> Result<Self, IrqError> {
        if range.start >= range.end || range.end > MAX_IRQS {
            return Err(IrqError::InvalidArgument);
        }

        let count = NonZeroUsize::new(range.end - range.start).unwrap();

        if !ALLOCATOR.lock().assign(range.start, count) {
            return Err(IrqError::Busy);
        }

        Ok(Self { number: range })
    }

    pub fn free(irq: IrqNumber) {
        ALLOCATOR.lock().clear(irq.get());
    }

    pub fn iter(&self) -> impl Iterator<Item = IrqNumber> + use<> {
        (self.number.start..self.number.end).map(|i| IrqNumber(i as u32))
    }

    pub fn get(&self, index: usize) -> Option<IrqNumber> {
        if index >= self.number.end - self.number.start {
            None
        } else {
            Some(IrqNumber((self.number.start + index) as u32))
        }
    }
}
```

drop 时将未注册的 irq 释放

```rust
impl Drop for IrqReservation {
    fn drop(&mut self) {
        for irq in self.iter() {
            if IRQ_DESCRIPTORS.lookup(irq).is_none() {
                Self::free(irq);
            }
        }
    }
}
```

# 预留

对于 ISA 的 16 个 IRQ，在启动阶段预留编号并发布占位 descriptor，同时在各 CPU 的 vector 反查表中永久保留 `0x20 + irq` 的映射。

```rust
/// 启动阶段发布具有占位内容的 descriptor，不分配 IOAPIC/LAPIC mapping
pub(super) fn init_irqs() -> Result<(), IrqError> {
    let irqs = IrqReservation::reserve((0..IRQ_COUNT).into())?;

    for irq in irqs.iter() {
        IRQ_DESCRIPTORS.publish(IrqDescriptor::placeholder(irq))?;
        VectorManager::get().reserve(irq, irq.get() as u8 + 0x20)?;
    }

    *ISA_NUMBERS.lock_irqsave() = Some(irqs);

    Ok(())
}
```
