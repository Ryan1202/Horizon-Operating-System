
1. [APIC 初始化](1-apic.md)：Local APIC、I/O APIC 与 EOI 策略。
2. [Vector 管理](2-vector.md)：各 CPU 的向量资源、ISA 固定映射和反查。
3. [IRQ 编号](3-irq-number.md)：硬件编号、全局编号及 reservation。
4. [Table 与 descriptor](4-table.md)：发布、占位配置、引用和数据层级。
5. [Handle](5-handle.md)：注册、启用、关闭和注销。
6. [硬件接口](6-hardware-trait.md)：chip 回调及 domain 生命周期。
7. [Flow](7-flow.md)：硬件收尾、action 遍历及重入计数。

## 当前路径

启动时初始化 vector 表，为 ISA IRQ 0–15 发布占位 descriptor，并在所有 CPU 的反查数组中保留 `0x20..0x2f`。真实 I/O APIC / LAPIC mapping 到首次请求时才配置；ISA 路由表的空项仍表示不可用。

`request_irq()` 注册 action，首个 action 激活 domain，但返回时新 action 仍禁用。驱动通过 handle 显式启用；增加共享 action 不屏蔽现有路由。硬件入口通过 `vector -> IrqNumber -> Arc<IrqDescriptor>` 找到配置，再由 flow 调用 handler。

非最后一个共享 handle 注销后，其禁用节点留在链中；最后一个 handle 执行 `Stopping -> mask -> drain -> mask -> synchronize -> deactivate`，然后摘除并回收链表。descriptor 和 mapping 仍保留，普通注销也不归还 LAPIC vector。撤销发布和释放 mapping 是另外的生命周期步骤。

## 阅读时需要区分

- `State.active` 统计尚未注销的 handler，包含未启用的 handler；action 的 `enabled` 决定是否调用它
- `in_progress` 在当前 flow 中统计尚未消费的处理轮次，重入由已有执行者继续处理；它不统计 `skip()` 路径
- `disable_irq()` 和非最后一个共享 handle 的注销不是等待旧回调完成的同步接口
- 目前 hardirq 路径仍取得表锁、状态锁及部分 chip 锁；无锁遍历 action 不等于整个入口无锁。
