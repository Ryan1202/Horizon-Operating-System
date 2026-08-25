use core::fmt::Debug;

use alloc::boxed::Box;

use crate::kernel::memory::kmalloc::Kmalloc;

pub struct Executable {
    bytecode: Box<[u8], Kmalloc>,
}

impl Debug for Executable {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(f, "Executable")
    }
}

impl Executable {
    pub fn new(bytecode: &[u8]) -> Self {
        unsafe {
            let mut dst = Box::new_uninit_slice_in(bytecode.len(), Kmalloc::default());
            dst.assume_init_mut().copy_from_slice(bytecode);
            Self {
                bytecode: dst.assume_init(),
            }
        }
    }
}
