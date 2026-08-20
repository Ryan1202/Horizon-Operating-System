#[repr(C, packed)]
pub struct ProcessorLocalApic {
    _type: u8,
    length: u8,
    /// APIC Processor UID
    ///
    /// 当 ACPI 命名空间中某个处理器设备对象的 _UID 子对象求值得到的数值与此字段中的数值相等时，
    /// 操作系统就将这个 Local APIC Structure 与该处理器对象关联起来。
    ///
    /// 或者，对于使用 Processor 声明操作符定义的处理器， 如果其中的 ProcessorId 与该字段的
    /// 数值相等，操作系统也会建立这种关联。
    ///
    /// 注意：目前已经不推荐使用 Processor 声明操作符
    acpi_processor_uid: u8,
    /// 处理器的 Local APIC ID
    pub apic_id: u8,
    pub flags: LocalApicFlags,
}

#[repr(transparent)]
pub struct LocalApicFlags(u32);

impl LocalApicFlags {
    /// 是否就绪标志
    ///
    /// 如果该标志为 `false`，且 `online_capble` 为 `true`，则可以在系统运行时启用该处理器
    ///
    /// 如果该标志为 `false`，且 `online_capble` 为 `false`，则该处理器无法启用
    pub const fn enabled(&self) -> bool {
        self.0 & 1 != 0
    }

    /// 可上线标志
    ///
    /// 该标志只在 `enabled` 为 `false` 时才有意义，表示该处理器是否可以在系统运行时启用
    pub const fn online_capable(&self) -> bool {
        self.0 & 2 != 0
    }
}
