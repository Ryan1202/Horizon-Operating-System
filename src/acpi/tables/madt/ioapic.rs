#[repr(C, packed)]
pub struct IoApic {
    _type: u8,
    length: u8,
    /// I/O APIC ID
    pub ioapic_id: u8,
    reserved: u8,
    /// I/O APIC 地址
    ///
    /// 用于访问 I/O APIC 寄存器的 32 位地址，每个 I/O APIC 都有一个唯一的地址
    pub ioapic_address: u32,
    /// I/O APIC 全局系统中断基址
    ///
    /// 指定该 I/O APIC 的中断输入引脚所对应的第一个 Global System Interrupt（GSI）编号
    ///
    /// 该 I/O APIC 拥有多少个中断输入，由 I/O APIC 的 MaxRedirEntry 寄存器确定
    pub global_system_interrupt_base: u32,
}
