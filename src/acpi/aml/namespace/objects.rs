use core::{
    ffi::CStr,
    fmt::{self},
    ptr::NonNull,
};

use alloc::boxed::Box;

use crate::{
    acpi::aml::{
        evaluator::Evaluatable,
        executor::Executable,
        namespace::{
            NameSpace,
            data::{Package, VarPackage},
        },
    },
    kernel::memory::kmalloc::Kmalloc,
    lib::rust::spinlock::SpinlockRaw,
};

#[derive(Clone)]
pub enum Object {
    // 数据类型
    Integer(u64),
    String(Box<[i8], Kmalloc>),
    Buffer(Box<[u8], Kmalloc>),
    Package(Package),
    VarPackage(VarPackage),
    Revision,
    // 节点类型
    Alias(NonNull<NameSpace>),
    BankField(Box<BankField, Kmalloc>),
    CreateField(Box<CreateField, Kmalloc>),
    DataTableRegion(Box<DataTableRegion, Kmalloc>),
    Device,
    Event,
    External(External),
    FieldUnit(FieldUnit),
    Method(Method),
    Mutex(Mutex),
    ObjectReference(Box<Evaluatable, Kmalloc>),
    OperationRegion(Box<OperationRegion, Kmalloc>),
    PowerResource(PowerResource),
    Processor,
    Scope,
    ThermalZone,
}

impl fmt::Debug for Object {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Object::Integer(int) => write!(f, "{:#x}", int),
            Object::String(str) => {
                write!(f, "String({:?})", unsafe { CStr::from_ptr(str.as_ptr()) })
            }
            Object::Buffer(buffer) => write!(f, "Buffer({:?})", buffer),
            Object::Package(package) => write!(f, "{:?}", package),
            Object::VarPackage(var_package) => write!(f, "{:?}", var_package),
            Object::Revision => write!(f, "Revision"),
            Object::Device => write!(f, "Device"),
            Object::Alias(alias) => write!(f, "Alias({:?})", alias),
            Object::CreateField(create_field) => write!(f, "{:?}", create_field),
            Object::DataTableRegion(region) => write!(f, "{:?}", region),
            Object::Event => write!(f, "Event"),
            Object::External(external) => write!(f, "{:?}", external),
            Object::Method(method) => write!(f, "{:?}", method),
            Object::Mutex(mutex) => write!(f, "{:?}", mutex),
            Object::FieldUnit(field_unit) => write!(f, "{:?}", field_unit),
            Object::BankField(bank_field) => write!(f, "{:?}", bank_field),
            Object::ObjectReference(reference) => write!(f, "{:?}", reference),
            Object::OperationRegion(operation_region) => write!(f, "{:?}", operation_region),
            Object::PowerResource(power_resource) => {
                write!(f, "{:?}", power_resource)
            }
            Object::Processor => write!(f, "Processor"),
            Object::Scope => write!(f, "Scope"),
            Object::ThermalZone => write!(f, "ThermalZone"),
        }
    }
}
#[derive(Debug, Clone)]
pub struct Method {
    pub sync_level: u8,
    pub serialize: bool,
    pub arg_count: u8,
    pub bytecode: Executable,
}

#[derive(Debug, Clone)]
pub enum RegionSpace {
    SystemMemory,
    SystemIO,
    PciConfig,
    EmbeddedControl,
    SmBus,
    Userdefined(u8),
}

#[derive(Debug, Clone)]
pub struct OperationRegion {
    pub region_space: RegionSpace,
    pub offset: Box<Evaluatable, Kmalloc>,
    pub len: Box<Evaluatable, Kmalloc>,
}

#[derive(Debug, Clone, Copy)]
pub enum FieldAccessType {
    Any,
    Byte,
    Word,
    Dword,
    Qword,
    Block,
    SmbSendRecv,
    SmbQuickAccess,
}

impl TryFrom<u8> for FieldAccessType {
    type Error = ();

    fn try_from(value: u8) -> Result<Self, ()> {
        match value {
            0 => Ok(FieldAccessType::Any),
            1 => Ok(FieldAccessType::Byte),
            2 => Ok(FieldAccessType::Word),
            3 => Ok(FieldAccessType::Dword),
            4 => Ok(FieldAccessType::Qword),
            5 => Ok(FieldAccessType::Block),
            6 => Ok(FieldAccessType::SmbSendRecv),
            7 => Ok(FieldAccessType::SmbQuickAccess),
            _ => Err(()),
        }
    }
}

#[derive(Debug, Clone, Copy)]
pub enum FieldUpdateRule {
    Preserve,
    WriteAsOnes,
    WriteAsZeros,
}

impl TryFrom<u8> for FieldUpdateRule {
    type Error = ();

    fn try_from(value: u8) -> Result<Self, ()> {
        match value {
            0 => Ok(FieldUpdateRule::Preserve),
            1 => Ok(FieldUpdateRule::WriteAsOnes),
            2 => Ok(FieldUpdateRule::WriteAsZeros),
            _ => Err(()),
        }
    }
}

#[derive(Debug)]
pub struct FieldUnit {
    pub region: NonNull<NameSpace>,
    pub access_type: FieldAccessType,
    pub lock: Option<SpinlockRaw>,
    pub update_rule: FieldUpdateRule,
    pub length: u32,
}

impl Clone for FieldUnit {
    fn clone(&self) -> Self {
        Self {
            region: self.region,
            access_type: self.access_type.clone(),
            lock: self.lock.is_some().then_some(SpinlockRaw::new_unlocked()),
            update_rule: self.update_rule.clone(),
            length: self.length,
        }
    }
}

#[derive(Debug, Clone)]
pub struct BankField {
    pub bank: NonNull<NameSpace>,
    pub bank_value: Evaluatable,
    pub field: FieldUnit,
}

impl RegionSpace {
    pub fn from_byte(byte: u8) -> Self {
        match byte {
            0x00 => RegionSpace::SystemMemory,
            0x01 => RegionSpace::SystemIO,
            0x02 => RegionSpace::PciConfig,
            0x03 => RegionSpace::EmbeddedControl,
            0x04 => RegionSpace::SmBus,
            _ => RegionSpace::Userdefined(byte),
        }
    }
}

#[derive(Debug)]
pub struct Mutex {
    lock: SpinlockRaw,
    level: u8,
}

impl Clone for Mutex {
    fn clone(&self) -> Self {
        Self {
            lock: SpinlockRaw::new_unlocked(),
            level: self.level,
        }
    }
}

impl Mutex {
    pub const fn new(level: u8) -> Self {
        Mutex {
            lock: SpinlockRaw::new_unlocked(),
            level,
        }
    }
}

#[derive(Debug, Clone)]
pub struct PowerResource {
    pub system_level: u8,
    pub resource_order: u16,
}

#[derive(Debug, Clone)]
pub enum CreateFieldType {
    Bit(Evaluatable),
    Byte(Evaluatable),
    Word(Evaluatable),
    Dword(Evaluatable),
    Qword(Evaluatable),
    ArbitraryLength {
        index: Evaluatable,
        num_bits: Evaluatable,
    },
}

#[derive(Debug, Clone)]
pub struct CreateField {
    pub field_type: CreateFieldType,
    pub source: Evaluatable,
}

#[derive(Debug, Clone)]
pub struct DataTableRegion {
    pub signature: Evaluatable,
    pub oem_id: Evaluatable,
    pub oem_table_id: Evaluatable,
}

#[derive(Debug, Clone)]
pub enum ExternalObjectType {
    Uninitialized,
    Integer,
    String,
    Buffer,
    Package,
    FieldUnit,
    Device,
    Event,
    Method,
    Mutex,
    OperationRegion,
    PowerResource,
    ThermalZone,
    BufferField,
    DebugObject,
    Unknown(u8),
}

impl From<u8> for ExternalObjectType {
    fn from(value: u8) -> Self {
        match value {
            0 => Self::Uninitialized,
            1 => Self::Integer,
            2 => Self::String,
            3 => Self::Buffer,
            4 => Self::Package,
            5 => Self::FieldUnit,
            6 => Self::Device,
            7 => Self::Event,
            8 => Self::Method,
            9 => Self::Mutex,
            10 => Self::OperationRegion,
            11 => Self::PowerResource,
            13 => Self::ThermalZone,
            14 => Self::BufferField,
            16 => Self::DebugObject,
            value => Self::Unknown(value),
        }
    }
}

impl From<ExternalObjectType> for u8 {
    fn from(object_type: ExternalObjectType) -> u8 {
        match object_type {
            ExternalObjectType::Uninitialized => 0,
            ExternalObjectType::Integer => 1,
            ExternalObjectType::String => 2,
            ExternalObjectType::Buffer => 3,
            ExternalObjectType::Package => 4,
            ExternalObjectType::FieldUnit => 5,
            ExternalObjectType::Device => 6,
            ExternalObjectType::Event => 7,
            ExternalObjectType::Method => 8,
            ExternalObjectType::Mutex => 9,
            ExternalObjectType::OperationRegion => 10,
            ExternalObjectType::PowerResource => 11,
            ExternalObjectType::ThermalZone => 13,
            ExternalObjectType::BufferField => 14,
            ExternalObjectType::DebugObject => 16,
            ExternalObjectType::Unknown(value) => value,
        }
    }
}

#[derive(Debug, Clone)]
pub struct External {
    pub object_type: ExternalObjectType,
    pub arg_count: u8,
}
