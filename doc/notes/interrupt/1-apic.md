# Local APIC 初始化

Local APIC 最早在 486 时期作为独立芯片出现，为了在存在多个 CPU 的系统中能够正常的处理中断以及发送处理器间中断 (IPI) 。后来集成进了 CPU 内部就改叫了 xAPIC，x2APIC 则是 xAPIC 的升级版，出现时间就更晚了。根据[英特尔的计划](https://www.intel.com/content/www/us/en/developer/articles/technical/software-security-guidance/technical-documentation/xapic-deprecation-plan.html)，xAPIC 即将在代号 Nova Lake 的消费级处理器中正式废弃，而 AMD 似乎没有此计划。

在 `MADT` 头部有一个 `Local Interrupt Controller Address` 字段，在 x86 中指的就是 Local APIC 的地址。不过 x86 有一个 MSR 寄存器 `IA32_APIC_BASE` 可以直接从中读取出地址更方便，这一寄存器允许修改 Local APIC 的起始地址，不过正常情况下用不到只需要读取。

具体格式如下：

| 63:MAXAPICADDR | MAXAPICADDR-1:12 | 11            | 10               | 9    | 8        | 7:0  |
| -------------- | ---------------- | ------------- | ---------------- | ---- | -------- | ---- |
| 保留           | APIC 起始地址    | APIC 全局使能 | 启用 x2APIC 模式 | 保留 | BSP 标志 | 保留 |
|                |                  | 可读写        |                  |      | 可读写   |      |

`MAXAPICADDR` 在正常情况下和最大物理地址是一样的，所以通过 `CPUID` 指令可以获取到。

如果只设置第 11 位就会进入 xAPIC 模式，此时再设置第 10 位就可以进入 x2APIC模式。

## xAPIC 准备

对于 Local APIC 的 MMIO 地址，如果有 ACPI 则使用 ACPI 提供的地址，否则使用默认地址 `0XFEE00000` ，这是英特尔 SDM 里规定的在CPU 启动或者重启后的地址。然后读取 `IA32_APIC_BASE` 比对是否一致，不一致则直接 panic

```rust
fn xapic_init_bsp() -> Result<(), IrqError> {
    let base = unsafe { rdmsr(IA32_APIC_BASE) };

    // 获取 xAPIC 地址宽度，默认 36 位
    let address_bits = if __cpuid(0x80000000).eax >= 0x80000008 {
        __cpuid(0x80000008).eax & 0xff
    } else {
        36
    };

    assert!(
        (12..64).contains(&address_bits),
        "unsupported APIC address width"
    );

    let address_mask = ((1u64 << address_bits) - 1) & !0xfff;
    let address = base & address_mask;

    let expected_addr = unsafe { (*BOOT_CAPABILITIES.get()).lapic_address } as u64;
    assert!(
        address == expected_addr,
        "APIC address mismatch: MSR provided address {:#x} != Default address from ACPI / Standard {:#x}",
        address,
        expected_addr
    );

    let address = PhysAddr::new(address as usize);

    if base & GLOBAL_ENABLE == 0 {
        unsafe { wrmsr(IA32_APIC_BASE, base | GLOBAL_ENABLE) };
    }

    LocalApic::init_xapic_bsp(address)
}
```

由于 xAPIC 使用了 MMIO 寄存器，所以在初始化之前还要先将这段内存映射建立好

```rust
pub(super) fn new_bsp(address: PhysAddr) -> Result<Self, IrqError> {
    let frame = address.to_frame_number();
    let offset = address.page_offset();

    let page = PageAllocOptions::mmio(
        frame,
        const { NonZero::new(1).unwrap() },
        PageCacheType::Uncached,
    )
    .allocate()?;
    let page = ManuallyDrop::new(page);

    // 移动 Pages 或扩容 Vec 不改变页映射的虚拟地址。
    let base = (page.start_addr() + offset).as_mut_ptr();
    let base = AtomicPtr::new(base);

    Ok(Self { base })
}
```

```rust
pub(crate) fn init_xapic_bsp(address: PhysAddr) -> Result<(), IrqError> {
    let lapic = Self::init(LapicType::XApic(MmioRegs::new_bsp(address)?))?;
    unsafe { LAPIC.get().write(MaybeUninit::new(lapic)) };
    Ok(())
}
```

当前 `MmioRegs` 只保存寄存器基址，映射通过 `ManuallyDrop<Pages>` 保持常驻，避免函数返回时释放页面。BSP 初始化后把 `LocalApic` 写入全局变量 `LAPIC`；AP 路径复制寄存器访问配置，并对当前 CPU 再执行一次 `init()`。虽然地址相同，但是各个 CPU 访问的都是自己的 Local APIC

## x2APIC 准备

如果要启用 x2APIC 模式，需要先检查 `CPUID.01H:ECX[21]` 判断是否支持 x2APIC，然后设置 `IA32_APIC_BASE[10]`

对于普通的外部设备来说 MMIO 比起传统的 io 指令性能会更好，所以 xAPIC 使用了 MMIO 寄存器，但是由于 local APIC 来说本来就集成进 CPU 内部了，所以升级的 x2APIC 使用了 MSR 寄存器，而且对 APIC MSR 寄存器的 `wrmsr` 是 `Relaxed` 顺序

但是由于启用 x2APIC 后 APIC ID 可能大于等于 255 （即使没有那么多逻辑 CPU 核心也可能出现），而 I/O APIC 的 `Destination` 字段只支持 8 位 ID，所以此时 VT-d 的 Interrupt Remapping 功能就成了一个比较重要的依赖功能。虽然不使用 Interrupt Remapping 也可以正常使用 x2APIC 模式的功能，但是外部中断就没法被分发到 APIC ID >= 255 的核心了

在 BIOS 交给 OS 时如果 APIC ID 都小于 255 那就会切换到 xAPIC 模式再交给 OS，所以直接检查当前模式就可以了

```rust
fn x2apic_init_bsp() -> Result<(), IrqError> {
    let base = unsafe { rdmsr(IA32_APIC_BASE) };

    // 按当前模式选择路径；尚未启用 x2APIC 时继续使用 xAPIC。
    if base & X2APIC_ENABLE == 0 {
        return xapic_init_bsp();
    }

    unimplemented!("x2APIC mode is not implemented");
}
```

## 初始化

xAPIC 和 x2APIC 的寄存器大致相同，只是访问方式不一样，所以我通过 trait 来抽象访问方式从而共用初始化代码

```rust
pub(super) trait Regs {
    fn read_common(&self, reg: CommonReg) -> u32;
    fn write_common(&self, reg: CommonReg, value: u32);
}
```

```rust
let reg: &dyn Regs = match &lapic_type {
    LapicType::XApic(mmio) => mmio,
    LapicType::X2Apic(msr) => msr,
};
```

初始化的第一步需要先将其关闭好修改配置，通过 SVR 寄存器的 SOFTWARE_ENABLE 可以做到临时关闭

```rust
let svr = reg.read_common(Svr);
reg.write_common(Svr, svr & !SOFTWARE_ENABLE);
```

通过读取 Version 寄存器可以得知支持的 LVT 个数，部分 LVT 的支持情况需要通过个数来判断

```rust
let max_lvt_entry = (reg.read_common(Version) >> 16) & 0xff;
```

具体判断方式如下：

```rust
pub(super) const fn supported(self, max_lvt: u32) -> bool {
    match self {
        Self::Timer | Self::Lint0 | Self::Lint1 => true,
        Self::Error => max_lvt >= 3,
        Self::Performance => max_lvt >= 4,
        Self::Thermal => max_lvt >= 5,
        Self::Cmci => max_lvt >= 6,
    }
}
```

然后将所有支持的 LVT 屏蔽，并把向量字段设为合法的 `ERROR_VECTOR` 占位值。普通 LVT 后续必须先绑定自己的向量，才能解除屏蔽。

```rust
for entry in [
    LvtEntry::Error,
    LvtEntry::Timer,
    LvtEntry::Lint0,
    LvtEntry::Lint1,
    LvtEntry::Performance,
    LvtEntry::Thermal,
    LvtEntry::Cmci,
] {
    if entry.supported(max_lvt_entry) {
        reg.write_common(entry.get_reg(), LVT_MASK | ERROR_VECTOR as u32);
    }
}
```

然后将错误状态和优先级都清零

```rust
if LvtEntry::Error.supported(max_lvt_entry) {
    reg.write_common(Esr, 0);
    let _ = reg.read_common(Esr);
}

reg.write_common(Tpr, 0);
```

Local APIC 会出现伪中断，伪中断的处理程序不能恢复 EOI 而应该直接返回，所以专门分配了一个中断向量

```rust
// 使用广播 EOI，保留其它位，关闭软件启用和 EOI broadcast suppression
let mut svr = svr & !(0xff | SOFTWARE_ENABLE | SUPPRESS_EOI_BROADCAST);
svr |= SPURIOUS_VECTOR as u32;
reg.write_common(Svr, svr);
```

最后写入配置保存

```rust
Ok(Self {
    max_lvt_entry,
    lapic_type,
})
```

# I/O APIC 初始化

I/O APIC 不像 Local APIC 能直接在 SDM 里找到详细信息，可以参考的只有 82093AA （最早的 I/O APIC 芯片） 的芯片手册以及 ICH 里新版的寄存器定义，由于后续的 I/O APIC 都需要兼容前面的芯片所以直接看这个也没问题

I/O APIC 位于 CPU 之外，现在都集成在了南桥中，负责处理来自外部的 IRQ 并分发给不同的 CPU，作用类似传统的 PIC 但是支持多核。因此 I/O APIC 的寄存器是所有 CPU 都可见的，单个 I/O APIC 能处理的 IRQ 数量有上限，因此可能存在多个 I/O APIC。

由于可以存在多个 I/O APIC 协同工作，所以源自外部的 IRQ 被编码为唯一的 GSI ( Global System Interrupt ) 来管理

## 寄存器

I/O APIC 的大部分寄存器都是通过间接访问的，直接映射到内存的寄存器有：

- IOREGSEL - 选择要访问的寄存器
- IOWIN - 要读/写的寄存器数据

在 ICH 的文档里，还能找到新的 I/O APIC 还有一个 EOI 寄存器：

- EOIR

早期的 APIC 体系中，回应中断都是靠直接写入 Local APIC 的 EOI 寄存器来通知 I/O APIC，由 Local APIC 在整个总线上广播 EOI 信息的。后来为了避免大量的广播，I/O APIC 也引入了 EOI 寄存器，只要 Local APIC 支持抑制 EOI 广播且 I/O APIC 支持 EOI，就可以通过再写入一次 I/O APIC 来避免广播。

根据 I/O APIC 的版本号可以判断是否支持：最早的 82093AA 的版本号是 0x1X，支持 EOI 的 I/O APIC 版本号则是 >= 0x20

## 初始化

同样，I/O APIC 的 MMIO 地址等信息都可以在 ACPI 的 MADT 表中找到，所以我选择先读取 ACPI 统计出所有 I/O APIC 的地址范围，一次性将它们映射后再进行初始化：

```rust
pub fn init(&self, ioapics: &[IoApicInfo]) -> Result<(), IrqError> {
    let mut guard = self.inner.lock_irqsave();
    if guard.is_some() {
        return Err(IrqError::Busy);
    }

    let mut apics = Box::<[Arc<IoApic, Kmalloc>], Kmalloc>::new_uninit_slice_in(
        ioapics.len(),
        Kmalloc::default(),
    );

    let range = ioapics
        .iter()
        .fold(None, |range, info| {
            let frame = PhysAddr::new(info.address).to_frame_number();
            let Some(range) = range else {
                return Some(frame..frame + 1);
            };

            if frame < range.start {
                Some(frame..range.end)
            } else if frame >= range.end {
                Some(range.start..frame + 1)
            } else {
                Some(range)
            }
        })
        .expect("IOAPIC list is empty");

    let count = NonZeroUsize::new(range.end.get() - range.start.get()).unwrap();
    let pages = PageAllocOptions::mmio(range.start, count, PageCacheType::Uncached)
        .allocate()
        .expect("failed to allocate IOAPIC MMIO page");

    for (index, info) in ioapics.iter().enumerate() {
        let offset = info.address - PhysAddr::from_frame_number(range.start).as_usize();

        let base = unsafe { pages.get_ptr::<u8>().byte_add(offset) };
        let apic = IoApic::new(info, base).expect("failed to initialize IOAPIC");

        let apic = Arc::new_in(apic, Kmalloc::default());
        apics[index].write(apic);
    }

    // SAFETY: 所有槽位已初始化，失败路径已清理前缀并返回
    let apics = unsafe { apics.assume_init() };

    // 校验全部完成后才编程硬件，覆盖最大下标对应的最后一个 pin
    for apic in &apics {
        let regs = apic.regs.lock_irqsave();

        for pin in 0..apic.pin_count {
            regs.set_redirection_entry(pin as u8, RedirectionEntry(MASK as u64));
        }
    }

    *guard = Some(IoApicInner {
        apics,
        _pages: pages,
    });

    Ok(())
}
```

I/O APIC 的初始化非常简单，将所有条目都设置为屏蔽就好了。绝大部分代码其实是在准备内存映射

对于 单个I/O APIC 芯片，对应 `IoApic` 类型，需要做的仅仅是读取版本、获取最大可重定向的 IRQ 个数将其转换为 GSI 中断号范围

```rust
pub struct IoApic {
    id: ApicId,
    regs: Spinlock<IoApicRegs>,
    gsi_base: u32,
    version: u8,
    pin_count: u32,
}

impl IoApic {
    fn new(info: &IoApicInfo, base: NonNull<u8>) -> Result<Self, IrqError> {
        let regs = Spinlock::new(IoApicRegs::new(base));
        let version = regs.lock_irqsave().read(regs::VERSION);

        let pin_count = ((version >> 16) & 0xff) + 1;
        if pin_count > MAX_PINS as u32 {
            return Err(IrqError::Unsupported);
        }

        let gsi_base = info.gsi_base.get();

        Ok(Self {
            id: info.id,
            regs,
            gsi_base,
            version: version as u8,
            pin_count,
        })
    }
}
```

由于 I/O APIC 是多个核心共用的存在竞争关系，所以寄存器的访问方法通过自旋锁保护起来

# 启用 Local APIC

完成 Local APIC 和 I/O APIC 的初始化之后，就可以启用 Local APIC 了。

在完成 I/O APIC 的初始化之后才启用 Local APIC 的一个重要原因就是要检查所有 APIC 是否都支持 EOI。首先检查 I/O APIC

```rust
let all_support_eoi = IoApics::get().all_support_eoi().ok_or(IrqError::NotFound)?;
```

对应的实现是

```rust
/// None 表示集合尚未初始化；空集合使用广播模式
pub fn all_support_eoi(&self) -> Option<bool> {
    let guard = self.inner.lock_irqsave();
    let state = guard.as_ref()?;

    Some(!state.apics.is_empty() && state.apics.iter().all(|apic| apic.supports_eoi()))
}
```

```rust
pub const fn supports_eoi(&self) -> bool {
    self.version >= 0x20
}
```

由于这里 `apics` 是使用数组保存的，可以使用迭代器的方法方便的实现所有 I/O APIC 的检查。当然，Local APIC 也需要检查是否支持 EOI 抑制，不过由于是否广播是由每个 Local APIC 自己决定的，所以不需要所有 Local APIC 都支持。（理论上单路 CPU 封装内的所有 CPU 核心应该都支持，不过我不确定多路 CPU 是否一致）

```rust
let directed = all_support_eoi && lapic.supports_eoi_suppression();

// SAFETY: 当前 CPU 尚未发布为路由候选，且能力检测已完成
unsafe { lapic.set_eoi_broadcast_suppressed(directed) };
```

```rust
pub fn supports_eoi_suppression(&self) -> bool {
    self.reg().read_common(Version) & SUPPRESS_EOI_BROADCASE_SUPPORT != 0
}
```

```rust
/// # Safety
///
/// 架构层须在 CPU 开始接收中断前调用。启用抑制时已确认全部 IOAPIC 支持显式 EOI，
/// 且当前 LAPIC 支持该位。不能在活动路由存在时切换
pub(crate) unsafe fn set_eoi_broadcast_suppressed(&self, suppressed: bool) {
    let reg = self.reg();
    let svr = reg.read_common(Svr);
    reg.write_common(
        Svr,
        if suppressed {
            svr | SUPPRESS_EOI_BROADCAST
        } else {
            svr & !SUPPRESS_EOI_BROADCAST
        },
    );
}
```

完成这一特性的处理之后就可以解除软件禁用了

```rust
lapic.software_enable();
```

```rust
pub(crate) fn software_enable(&self) {
    let reg = self.reg();
    reg.write_common(Svr, reg.read_common(Svr) | SOFTWARE_ENABLE);
}
```

最后向向量管理器注册一下当前 CPU ，由于 I/O APIC 将 IRQ 进行了重定向，所以 IRQ 不再和中断向量号绑死，可以进行动态分配，向量管理器正是负责这一工作。每个 CPU 的中断向量号资源都是独立的，这意味着同一个向量号在不同 CPU 核心上可以对应不同 IRQ。