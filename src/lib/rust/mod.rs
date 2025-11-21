pub mod list;
pub mod spinlock;

#[macro_export]
macro_rules! container_of {
    ($ptr:expr, $container:ty, $field:ident) => {{
        use core::mem::offset_of;

        let _ptr: NonNull<_> = $ptr;
        let offset = offset_of!($container, $field);
        unsafe { _ptr.byte_offset(-(offset as isize)).cast::<$container>() }
    }};
}

#[macro_export]
macro_rules! container_of_enum {
    ($ptr:expr, $container:ty, $field:expr) => {{
        use core::mem::offset_of;

        let _ptr: NonNull<_> = $ptr;
        let offset = offset_of!($container, $field);
        unsafe { _ptr.byte_offset(-(offset as isize)).cast::<$container>() }
    }};
}
