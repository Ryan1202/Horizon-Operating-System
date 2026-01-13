#![no_std]

use core::iter::Iterator;
use core::marker::Copy;
use core::option::Option;
use core::option::Option::{None, Some};

#[repr(C)]
pub struct MemoryBitmap {
    pub bitmap: *mut u8,
    pub size_in_bytes: usize,
}

#[export_name = "virtual_memory_bitmap"]
pub static mut VIRTUAL_MEMORY_BITMAP: MemoryBitmap = MemoryBitmap {
    bitmap: core::ptr::null_mut(),
    size_in_bytes: 0,
};

#[export_name = "physical_memory_bitmap"]
pub static mut PHYSICAL_MEMORY_BITMAP: MemoryBitmap = MemoryBitmap {
    bitmap: core::ptr::null_mut(),
    size_in_bytes: 0,
};

impl MemoryBitmap {
    #[no_mangle]
    pub unsafe fn new_vir(addr: *mut u8, size_in_bytes: usize) {
        VIRTUAL_MEMORY_BITMAP = MemoryBitmap {
            bitmap: addr,
            size_in_bytes,
        };
    }
    #[no_mangle]
    pub unsafe fn new_phy(addr: *mut u8, size_in_bytes: usize) {
        PHYSICAL_MEMORY_BITMAP = MemoryBitmap {
            bitmap: addr,
            size_in_bytes,
        };
    }

    unsafe fn read<T: Copy>(&self, index: usize) -> T {
        *(self.bitmap.add(index) as *const T)
    }

    unsafe fn reverse(&mut self, index: usize, bit: u8) {
        *(self.bitmap.add(index) as *mut u32) ^= 1 << bit;
    }

    pub fn alloc_single(&mut self) -> Option<usize> {
        let mut dword;

        for i in (0..(self.size_in_bytes)).step_by(4) {
            dword = unsafe { self.read::<u32>(i) };
            let bit = dword.leading_zeros();
            if bit != 32 {
                dword |= 1 << (31 - bit);
                unsafe {
                    self.reverse(i, bit as u8);
                }
                return Some(i * 32 + (31 - bit) as usize);
            }
        }
        None
    }
}
