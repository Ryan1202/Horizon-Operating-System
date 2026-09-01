use core::ptr::{read_volatile, with_exposed_provenance, write_volatile};

use alloc::vec::Vec;

use crate::{
    acpi::{
        AcpiArchInterface,
        aml::{
            evaluator::{AsEvaluated, data::Integer},
            executor::ExecuteContext,
            namespace::{
                Object,
                objects::{FieldAccessType, FieldUnit, OperationRegion, RegionSpace},
            },
        },
    },
    arch::ArchAcpi,
    kernel::memory::kmalloc::Kmalloc,
};

pub trait RegionAccess {
    fn read_u8(&self, offset: u64) -> u8;
    fn read_u16(&self, offset: u64) -> u16;
    fn read_u32(&self, offset: u64) -> u32;
    fn read_u64(&self, offset: u64) -> u64;
    fn write_u8(&self, offset: u64, value: u8);
    fn write_u16(&self, offset: u64, value: u16);
    fn write_u32(&self, offset: u64, value: u32);
    fn write_u64(&self, offset: u64, value: u64);
}

pub struct SystemMemory;

impl SystemMemory {
    fn read<T>(&self, offset: u64) -> T {
        unsafe { read_volatile(with_exposed_provenance::<T>(offset as usize)) }
    }

    fn write<T>(&self, offset: u64, value: T) {
        let ptr = with_exposed_provenance::<T>(offset as usize).cast_mut();
        unsafe { write_volatile(ptr, value) }
    }
}

impl RegionAccess for SystemMemory {
    fn read_u8(&self, offset: u64) -> u8 {
        self.read(offset)
    }
    fn read_u16(&self, offset: u64) -> u16 {
        self.read(offset)
    }
    fn read_u32(&self, offset: u64) -> u32 {
        self.read(offset)
    }
    fn read_u64(&self, offset: u64) -> u64 {
        self.read(offset)
    }
    fn write_u8(&self, offset: u64, value: u8) {
        self.write(offset, value)
    }
    fn write_u16(&self, offset: u64, value: u16) {
        self.write(offset, value)
    }
    fn write_u32(&self, offset: u64, value: u32) {
        self.write(offset, value)
    }
    fn write_u64(&self, offset: u64, value: u64) {
        self.write(offset, value)
    }
}

pub struct SystemIO;

impl RegionAccess for SystemIO {
    fn read_u8(&self, offset: u64) -> u8 {
        ArchAcpi::io_in_u8(offset as u16)
    }
    fn read_u16(&self, offset: u64) -> u16 {
        ArchAcpi::io_in_u16(offset as u16)
    }
    fn read_u32(&self, offset: u64) -> u32 {
        ArchAcpi::io_in_u32(offset as u16)
    }
    fn read_u64(&self, offset: u64) -> u64 {
        let lo = ArchAcpi::io_in_u32(offset as u16);
        let hi = ArchAcpi::io_in_u32((offset + 4) as u16);
        (hi as u64) << 32 | lo as u64
    }
    fn write_u8(&self, offset: u64, value: u8) {
        ArchAcpi::io_out_u8(offset as u16, value);
    }
    fn write_u16(&self, offset: u64, value: u16) {
        ArchAcpi::io_out_u16(offset as u16, value);
    }
    fn write_u32(&self, offset: u64, value: u32) {
        ArchAcpi::io_out_u32(offset as u16, value);
    }
    fn write_u64(&self, offset: u64, value: u64) {
        ArchAcpi::io_out_u32(offset as u16, value as u32);
        ArchAcpi::io_out_u32((offset + 4) as u16, (value >> 32) as u32);
    }
}

pub(in crate::acpi) fn with_region<F, R>(space: RegionSpace, f: F) -> R
where
    F: FnOnce(&dyn RegionAccess) -> R,
{
    match space {
        RegionSpace::SystemMemory => f(&SystemMemory),
        RegionSpace::SystemIO => f(&SystemIO),
        _ => unimplemented!("RegionSpace::{:?} not implemented", space),
    }
}

pub enum FieldUnitValue {
    Integer(Integer),
    Buffer32(Vec<u32, Kmalloc>),
    Buffer64(Vec<u64, Kmalloc>),
}

fn read_bits_u32(region: &dyn RegionAccess, base: u64, bit_offset: u64, bit_length: u64) -> u32 {
    let count = (bit_offset + bit_length).div_ceil(32);
    let mut integer = 0u32;
    let mut offset = bit_offset;
    let mut length = bit_length;

    for i in 0..count {
        let mut byte = u32::from_be(region.read_u32(base + i * 4));
        if i == 0 {
            byte &= (1 << (offset % 32)) - 1;
            offset -= offset.min(32);
        }
        if i == count - 1 {
            byte >>= 32 - offset - length;
        }
        let len = 32 - offset.min(32);
        length -= len;
        integer = (integer << len) | byte;
    }

    integer
}

fn read_bits_u64(region: &dyn RegionAccess, base: u64, bit_offset: u64, bit_length: u64) -> u64 {
    let count = (bit_offset + bit_length).div_ceil(64);
    let mut integer = 0u64;
    let mut offset = bit_offset;
    let mut length = bit_length;

    for i in 0..count {
        let mut byte = u64::from_be(region.read_u64(base + i * 8));
        if i == 0 {
            byte &= (1 << (offset % 64)) - 1;
            offset -= offset.min(64);
        }
        if i == count - 1 {
            byte >>= 64 - offset - length;
        }
        let len = 64 - offset.min(64);
        length -= len;
        integer = (integer << len) | byte;
    }

    integer
}

fn read_field(
    region: &dyn RegionAccess,
    base: u64,
    bit_offset: u64,
    bit_length: u64,
    access_type: FieldAccessType,
    bit_width: usize,
) -> Option<FieldUnitValue> {
    match access_type {
        FieldAccessType::Any => {
            if bit_width as u64 >= bit_length {
                if bit_width == 32 {
                    Some(FieldUnitValue::Integer(Integer::U32(read_bits_u32(
                        region, base, bit_offset, bit_length,
                    ))))
                } else {
                    Some(FieldUnitValue::Integer(Integer::U64(read_bits_u64(
                        region, base, bit_offset, bit_length,
                    ))))
                }
            } else {
                read_buffer(region, base, bit_offset, bit_length, bit_width)
            }
        }
        FieldAccessType::Byte => {
            let byte = region.read_u8(base);
            Some(int_value(byte as u64, bit_width))
        }
        FieldAccessType::Word => {
            let word = region.read_u16(base);
            Some(int_value(word as u64, bit_width))
        }
        FieldAccessType::Dword => {
            let dword = region.read_u32(base);
            Some(int_value(dword as u64, bit_width))
        }
        FieldAccessType::Qword if bit_width == 64 => {
            let qword = region.read_u64(base);
            Some(FieldUnitValue::Integer(Integer::U64(qword)))
        }
        FieldAccessType::Block => read_buffer(region, base, bit_offset, bit_length, bit_width),
        _ => unimplemented!(),
    }
}

fn read_buffer(
    region: &dyn RegionAccess,
    base: u64,
    bit_offset: u64,
    bit_length: u64,
    bit_width: usize,
) -> Option<FieldUnitValue> {
    let count = (bit_offset + bit_length).div_ceil(bit_width as u64);
    if bit_width == 32 {
        let mut buffer = Vec::with_capacity_in(count as usize, Kmalloc::default());
        for i in 0..count {
            buffer.push(read_bits_u32(
                region,
                base,
                bit_offset + i * bit_width as u64,
                bit_length,
            ));
        }
        Some(FieldUnitValue::Buffer32(buffer))
    } else {
        let mut buffer = Vec::with_capacity_in(count as usize, Kmalloc::default());
        for i in 0..count {
            buffer.push(read_bits_u64(
                region,
                base,
                bit_offset + i * bit_width as u64,
                bit_length,
            ));
        }
        Some(FieldUnitValue::Buffer64(buffer))
    }
}

fn int_value(value: u64, bit_width: usize) -> FieldUnitValue {
    if bit_width == 32 {
        FieldUnitValue::Integer(Integer::U32(value as u32))
    } else {
        FieldUnitValue::Integer(Integer::U64(value))
    }
}

impl AsEvaluated<Integer> for FieldUnit {
    fn evaluate(self, context: &mut ExecuteContext) -> Result<Integer, ()> {
        let (base, region) = resolve_region(&self, context)?;
        let _guard = self.lock.as_ref().map(|lock| lock.lock());

        with_region(region.region_space, |access| {
            let value = read_field(
                access,
                base,
                self.bit_offset as u64,
                self.bit_length as u64,
                self.access_type,
                context.revision().bit_width(),
            )
            .ok_or(())?;
            match value {
                FieldUnitValue::Integer(integer) => Ok(integer),
                _ => Err(()),
            }
        })
    }
}

pub(in crate::acpi) fn write_field_unit(
    field: &FieldUnit,
    value: Integer,
    context: &mut ExecuteContext,
) -> Option<()> {
    let Ok((base, region)) = resolve_region(field, context) else {
        return None;
    };
    let _guard = field.lock.as_ref().map(|lock| lock.lock());

    let raw: u64 = value.into();
    let bit_offset = field.bit_offset as u64;
    let bit_length = field.bit_length as u64;

    with_region(region.region_space, |access| {
        // 字节对齐：直接写入（与 read_field 的 Byte/Word/Dword/Qword 对称）
        if bit_offset % 8 == 0 && bit_length % 8 == 0 {
            let byte_offset = base + bit_offset / 8;
            match bit_length / 8 {
                1 => access.write_u8(byte_offset, raw as u8),
                2 => access.write_u16(byte_offset, (raw as u16).to_be()),
                4 => access.write_u32(byte_offset, (raw as u32).to_be()),
                8 => access.write_u64(byte_offset, raw.to_be()),
                _ => {}
            }
            return;
        }

        // 非对齐：用和 read_bits 同样的粒度做读-改-写
        // 找到覆盖 [bit_offset, bit_offset+bit_length) 的 u32 边界
        let byte_start = base + bit_offset / 8;
        let local_bit = bit_offset % 32;
        let count = (local_bit + bit_length).div_ceil(32) as usize;

        let mut word = 0u64;
        for i in 0..count {
            let v = u32::from_be(access.read_u32(byte_start + i as u64 * 4));
            word = (word << 32) | v as u64;
        }

        // 内存中的数据：
        //               bit_offset
        //                    |
        // | ........ | ......yy | yyyyyyyy | yyyyyyyy |
        // | yyyyyyyy | yyyyyyyy | yyyyyyyy | yyyy.... |
        //                                        \  /
        //                                       shift
        // mask:
        // | 00000000 | 00000011 | 11111111 | 11111111 |
        // | 11111111 | 11111111 | 11111111 | 11110000 |
        //
        // 要写入的数据：
        // | ........ | ........ | ..xxxxxx | xxxxxxxx |
        // | xxxxxxxx | xxxxxxxx | xxxxxxxx | xxxxxxxx |
        //
        // 1.a. word & !mask:
        // | ........ | ......00 | 00000000 | 00000000 |
        // | 00000000 | 00000000 | 00000000 | 0000.... |
        //
        // 1.b. (raw << shift) & mask:
        // | 00000000 | 000000xx | xxxxxxxx | xxxxxxxx |
        // | xxxxxxxx | xxxxxxxx | xxxxxxxx | xxxx0000 |
        //
        // 2. (word & !mask) | ((raw << shift) & mask):
        // | ........ | ......xx | xxxxxxxx | xxxxxxxx |
        // | xxxxxxxx | xxxxxxxx | xxxxxxxx | xxxx.... |

        let total_bits = count * 32;
        let shift = total_bits as u64 - local_bit - bit_length;
        let mask = ((1u64 << bit_length) - 1) << shift;
        word = (word & !mask) | ((raw << shift) & mask);

        // 写回
        for i in 0..count {
            let shift = (count - 1 - i) * 32;
            access.write_u32(byte_start + i as u64 * 4, ((word >> shift) as u32).to_be());
        }
    });

    Some(())
}

/// 从 FieldUnit 解析出 OperationRegion 的 base 地址，同时做边界检查
fn resolve_region<'a>(
    field: &FieldUnit,
    context: &mut ExecuteContext,
) -> Result<(u64, &'a OperationRegion), ()> {
    let region = unsafe { field.region.as_ref() }.object();
    let Object::OperationRegion(region) = region else {
        return Err(());
    };

    let base = match region.offset.clone().evaluate(context)? {
        Integer::U32(offset) => offset as u64,
        Integer::U64(offset) => offset,
    };

    let length = match region.len.clone().evaluate(context)? {
        Integer::U32(length) => length as u64,
        Integer::U64(length) => length,
    };

    if base + ((field.bit_offset + field.bit_length) as u64 / 8) >= base + length {
        return Err(());
    }

    Ok((base, region))
}
