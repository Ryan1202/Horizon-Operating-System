中断控制器驱动和内核的 IRQ 框架本身通过几个 trait 来协作

# IrqChip

`IrqChip` 是对单个中断控制器芯片的功能的抽象，还额外要求了 `Any` 以便在知道类型时转换回原始类型（比如 I/O APIC 可能会需要读取 Local APIC 的信息）

```rust
/// Core 只调用最外层 chip；parent 操作由 chip 根据硬件语义显式转发
pub trait IrqChip: Any + Send + Sync {
    /// 屏蔽中断
    fn mask(&self, data: &IrqData);
    /// 取消屏蔽中断
    fn unmask(&self, data: &IrqData);
    /// 确认接收到中断
    fn ack(&self, data: &IrqData);
    /// 中断处理已完成
    fn eoi(&self, data: &IrqData);
    /// 屏蔽并确认接收到中断
    fn mask_ack(&self, data: &IrqData) {
        self.mask(data);
        self.ack(data);
    }
}

impl dyn IrqChip {
    /// Domain 可从 IrqData 已保存的 chip 恢复具体控制器类型
    pub fn downcast_ref<T: IrqChip>(&self) -> Option<&T> {
        (self as &dyn Any).downcast_ref()
    }
}
```

# Domain

`Domain` 是指一组相同的中断控制器组成的 IRQ 域，比如 I/O APIC 会有一个跨越多个芯片统一管理的 IRQ 编号就需要纳入 Domain 的管理范围

```rust
/// 生命周期仅在可等待的管理上下文调用
pub trait Domain: Send + Sync {
    /// 构造当前 domain 的私有数据
    fn allocate(&self, irq: IrqNumber, arg: &dyn Any) -> Result<IrqData, IrqError>;
    /// 释放当前 domain 的私有数据
    fn free(&self, data: &IrqData);

    /// 成功后路由可用于 chip 回调，但中断源必须保持屏蔽
    fn activate(
        &self,
        irq: IrqNumber,
        data: &IrqData,
        affinity: &mut Affinity,
    ) -> Result<(), IrqError>;

    /// 只释放路由
    fn deactivate(&self, data: &IrqData);

    /// 源已屏蔽，等待本层已发起的事件完成
    fn synchronize(&self, data: &IrqData);
}
```

`Domain` 的调用可能涉及内存分配或自旋等待，所以应从允许 IRQ 管理操作的上下文调用

此外，框架还对 `activate` , `deactivate` , `synchronize` 方法进行了封装，穿透层级按正确的顺序调用

```rust
pub(super) fn activate(
    irq: IrqNumber,
    data: &IrqData,
    affinity: &mut Affinity,
) -> Result<(), IrqError> {
    if let Some(parent) = data.parent() {
        activate(irq, parent, affinity)?;
    }
    if let Err(error) = data.domain().activate(irq, data, affinity) {
        if let Some(parent) = data.parent() {
            deactivate(parent);
        }
        return Err(error);
    }
    Ok(())
}

pub(super) fn deactivate(data: &IrqData) {
    data.domain().deactivate(data);
    if let Some(parent) = data.parent() {
        deactivate(parent);
    }
}

pub(super) fn synchronize(data: &IrqData) {
    data.domain().synchronize(data);
    if let Some(parent) = data.parent() {
        synchronize(parent);
    }
}
```

激活按父到子进行；某一层激活失败时，由该层负责自己的失败清理，core 对已激活的父链执行 `deactivate()`。成功只表示硬件路由准备好，源必须仍保持屏蔽，最终由 handle 的 `enable_irq()` 开放。

同步和停用按子到父进行。释放 mapping 则由每层 `IrqData::drop()` 调用本层 `free()`，再随着 parent 字段析构继续向父层释放。`deactivate()` 和 `free()` 是不同阶段：当前 LAPIC domain 的 `deactivate()` 为空，保留 vector，`free()` 才归还它。

## I/O APIC 与 LAPIC 的协作

| 接口 | I/O APIC 层 | LAPIC 父层 |
| --- | --- | --- |
| `allocate` | 找 GSI 对应的 chip/pin，保存极性和触发方式，并构造 parent | 保存 scope，route 初始为空 |
| `activate` | 使用父 route 编程重定向项，保持 MASK | 首次分配 vector，后续复用 route，并写回实际 affinity |
| `synchronize` | 等 Delivery Status 清零，Level 还等 Remote IRR | 当前为空 |
| `deactivate` | 清除 pin 的 active 位 | 当前为空，保留 route |
| `free` | 当前为空 | 释放 route 对应的 vector；ISA 固定映射保留 |

core 的 chip 回调只调用最外层，不像 domain 包装函数那样自动遍历 parent。I/O APIC 的 Edge `ack` 显式调用父 LAPIC `eoi`；Level `eoi` 先调用父 LAPIC，再按当前 CPU 的 EOI 模式决定是否写 I/O APIC EOI 寄存器。

