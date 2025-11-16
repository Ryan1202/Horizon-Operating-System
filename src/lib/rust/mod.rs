pub mod list;
pub mod spinlock;

#[macro_export]
macro_rules! container_of {
    ($ptr:expr, $container:ty, $field:ident) => {{
        use core::mem::offset_of;

        let node: *mut ListNode<$container> = $ptr;
        let offset = offset_of!($container, $field);
        (node as *mut u8).offset(-(offset as isize)) as *mut $container
    }};
}
