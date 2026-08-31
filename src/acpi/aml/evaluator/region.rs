use core::ptr::{self, read_volatile, with_exposed_provenance};

use alloc::vec::Vec;

use crate::{
    acpi::aml::{
        evaluator::{AsEvaluated, data::Integer},
        executor::ExecuteContext,
        namespace::{
            Object,
            objects::{FieldAccessType, FieldUnit, RegionSpace},
        },
    },
    kernel::memory::kmalloc::Kmalloc,
};

pub enum FieldUnitValue {
    Integer(Integer),
    Buffer32(Vec<u32, Kmalloc>),
    Buffer64(Vec<u64, Kmalloc>),
}

pub struct SystemMemory;

impl SystemMemory {
    fn read_u32(base: *const u32, bit_offset: u64, bit_length: u64, bit_width: u64) -> u32 {
        let count = (bit_offset + bit_length).div_ceil(bit_width);
        let mut integer = 0;
        let mut offset = bit_offset;
        let mut length = bit_length;

        for i in 0..count {
            let mut byte = u32::from_be(unsafe { ptr::read_volatile(base) });
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

    fn read_u64(base: *const u64, bit_offset: u64, bit_length: u64, bit_width: u64) -> u64 {
        let count = (bit_offset + bit_length).div_ceil(bit_width);
        let mut integer = 0;
        let mut offset = bit_offset;
        let mut length = bit_length;

        for i in 0..count {
            let mut byte = u64::from_be(unsafe { ptr::read_volatile(base) });
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

    fn read_buffer(
        base: *const (),
        bit_offset: u64,
        bit_length: u64,
        bit_width: u64,
    ) -> Option<FieldUnitValue> {
        if bit_width == 32 {
            let count = (bit_offset + bit_length).div_ceil(bit_width);
            let mut buffer = Vec::with_capacity_in(count as usize, Kmalloc::default());

            for i in 0..count {
                let integer = Self::read_u32(
                    base as *const u32,
                    bit_offset + i * bit_width,
                    bit_length,
                    bit_width,
                );
                buffer.push(integer);
            }

            Some(FieldUnitValue::Buffer32(buffer))
        } else {
            let count = (bit_offset + bit_length).div_ceil(bit_width);
            let mut buffer = Vec::with_capacity_in(count as usize, Kmalloc::default());

            for i in 0..count {
                let integer = Self::read_u64(
                    base as *const u64,
                    bit_offset + i * bit_width,
                    bit_length,
                    bit_width,
                );
                buffer.push(integer);
            }

            Some(FieldUnitValue::Buffer64(buffer))
        }
    }

    pub fn read(
        address: u64,
        bit_offset: u64,
        bit_length: u64,
        access_type: FieldAccessType,
        bit_width: usize,
    ) -> Option<FieldUnitValue> {
        let ptr = with_exposed_provenance(address as usize);

        match access_type {
            FieldAccessType::Any => {
                if bit_width as u64 >= bit_length {
                    if bit_width == 32 {
                        let integer = Self::read_u32(
                            ptr as *const u32,
                            bit_offset,
                            bit_length,
                            bit_width as u64,
                        );

                        Some(FieldUnitValue::Integer(Integer::U32(integer)))
                    } else {
                        let integer = Self::read_u64(
                            ptr as *const u64,
                            bit_offset,
                            bit_length,
                            bit_width as u64,
                        );

                        Some(FieldUnitValue::Integer(Integer::U64(integer)))
                    }
                } else {
                    Self::read_buffer(ptr, bit_offset, bit_length, bit_width as u64)
                }
            }
            FieldAccessType::Byte => {
                let byte = unsafe { read_volatile(ptr as *const u8) };
                if bit_width == 32 {
                    Some(FieldUnitValue::Integer(Integer::U32(byte as u32)))
                } else {
                    Some(FieldUnitValue::Integer(Integer::U64(byte as u64)))
                }
            }
            FieldAccessType::Word => {
                let word = unsafe { read_volatile(ptr as *const u16) };
                if bit_width == 32 {
                    Some(FieldUnitValue::Integer(Integer::U32(word as u32)))
                } else {
                    Some(FieldUnitValue::Integer(Integer::U64(word as u64)))
                }
            }
            FieldAccessType::Dword => {
                let dword = unsafe { read_volatile(ptr as *const u32) };
                if bit_width == 32 {
                    Some(FieldUnitValue::Integer(Integer::U32(dword)))
                } else {
                    Some(FieldUnitValue::Integer(Integer::U64(dword as u64)))
                }
            }
            FieldAccessType::Qword if bit_width == 64 => {
                let qword = unsafe { read_volatile(ptr as *const u64) };
                Some(FieldUnitValue::Integer(Integer::U64(qword)))
            }
            FieldAccessType::Block => {
                Self::read_buffer(ptr, bit_offset, bit_length, bit_width as u64)
            }
            _ => unimplemented!(),
        }
    }

    pub fn write(&self, address: u64, data: &[u8]) -> Option<()> {
        let ptr = address as *mut u8;
        let slice = unsafe { core::slice::from_raw_parts_mut(ptr, data.len()) };
        slice.copy_from_slice(data);
        Some(())
    }
}

impl AsEvaluated<Integer> for FieldUnit {
    fn evaluate(self, context: &mut ExecuteContext) -> Result<Integer, ()> {
        let region = unsafe { self.region.as_ref() }.object();

        if let Object::OperationRegion(region) = region {
            let base = match region.offset.clone().evaluate(context)? {
                Integer::U32(offset) => offset as u64,
                Integer::U64(offset) => offset,
            };

            let length = match region.len.clone().evaluate(context)? {
                Integer::U32(length) => length as u64,
                Integer::U64(length) => length,
            };

            if base + ((self.bit_offset + self.bit_length) as u64 / 8) >= base + length {
                return Err(());
            }

            let _guard = if let Some(lock) = &self.lock {
                Some(lock.lock())
            } else {
                None
            };

            match region.region_space {
                RegionSpace::SystemMemory => SystemMemory::read(
                    base,
                    self.bit_offset as u64,
                    self.bit_length as u64,
                    self.access_type,
                    context.revision().bit_width(),
                )
                .ok_or(()),
                _ => unimplemented!("Only SystemMemory region space is implemented"),
            }
            .and_then(|value| {
                if let FieldUnitValue::Integer(integer) = value {
                    Ok(integer)
                } else {
                    Err(())
                }
            })
        } else {
            return Err(());
        }
    }
}
