pub mod list;
pub mod spinlock;

#[macro_export]
macro_rules! container_of {
    ($ptr:expr, $container:ty, $field:ident) => {{
        use core::mem::offset_of;

        let offset = offset_of!($container, $field);
        ($ptr as *mut u8).offset(-(offset as isize)) as *mut $container
    }};
}

#[macro_export]
macro_rules! container_of_enum {
    ($ptr:expr, $container:ty, $field:expr) => {{
        use core::mem::offset_of;

        let offset = offset_of!($container, $field);
        ($ptr as *mut u8).offset(-(offset as isize)) as *mut $container
    }};
}
