use core::ptr::NonNull;

pub struct IrqId<Source> {
    id: u32,
    source: NonNull<Source>,
}
