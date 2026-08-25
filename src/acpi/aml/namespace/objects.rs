use core::{
    ffi::CStr,
    fmt::{self, Write},
};

use alloc::boxed::Box;

use crate::{
    acpi::aml::{
        executor::Executable,
        namespace::data::{Package, VarPackage},
    },
    kernel::memory::kmalloc::Kmalloc,
    lib::rust::spinlock::SpinlockRaw,
};

pub enum Object {
    // 数据类型
    Integer(u64),
    String(Box<[i8], Kmalloc>),
    Buffer(Box<[u8], Kmalloc>),
    Package(Package),
    VarPackage(VarPackage),
    Revision,
    // 节点类型
    Device,
    Field,
    Method(Method),
    Mutex(Mutex),
    // Name(DataRefObject),
    FieldUnit(FieldUnit),
    OperationRegion,
    PowerResource(PowerResource),
    Processor,
    Region(OperationRegion),
    Scope,
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
            Object::Field => write!(f, "Field"),
            Object::Method(method) => write!(f, "{:?}", method),
            Object::Mutex(mutex) => write!(f, "{:?}", mutex),
            Object::FieldUnit(field_unit) => write!(f, "{:?}", field_unit),
            Object::OperationRegion => write!(f, "OperationRegion"),
            Object::PowerResource(power_resource) => {
                write!(f, "{:?}", power_resource)
            }
            Object::Processor => write!(f, "Processor"),
            Object::Region(region) => write!(f, "{:?}", region),
            Object::Scope => write!(f, "Scope"),
        }
    }
}

#[derive(Debug)]
pub struct Method {
    pub sync_level: u8,
    pub serialize: bool,
    pub arg_count: u8,
    pub bytecode: Executable,
}

#[derive(Debug)]
pub enum RegionSpace {
    SystemMemory,
    SystemIO,
    PciConfig,
    EmbeddedControl,
    SmBus,
    Userdefined(u8),
}

#[derive(Debug)]
pub struct OperationRegion {
    pub region_space: RegionSpace,
    pub offset: (),
    pub len: (),
}

#[derive(Debug)]
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

#[derive(Debug)]
pub enum FieldUpdateRule {
    Preserve,
    WriteAsOnes,
    WriteAsZeros,
}

#[derive(Debug)]
pub struct FieldUnit {
    pub access_type: FieldAccessType,
    pub lock: Option<SpinlockRaw>,
    pub update_rule: FieldUpdateRule,
    pub length: u32,
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

impl Mutex {
    pub const fn new(level: u8) -> Self {
        Mutex {
            lock: SpinlockRaw::new_unlocked(),
            level,
        }
    }
}

#[derive(Debug)]
pub struct PowerResource {
    pub system_level: u8,
    pub resource_order: u16,
}
