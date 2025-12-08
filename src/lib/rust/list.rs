// no external imports required

use core::{marker::PhantomData, ptr::NonNull};

use super::spinlock::Spinlock;

#[macro_export]
macro_rules! list_owner {
    ($var:ident, $container:ty, $field:ident) => {{ $crate::container_of!($var.cast::<ListNode<$container>>(), $container, $field) }};
}

#[macro_export]
macro_rules! list_first_owner {
    ($container:ty, $field:ident, $head:expr) => {{
        use $crate::list_owner;
        $head
            .head
            .and_then(|n| Some(list_owner!(n, $container, $field)))
    }};
}

#[macro_export]
macro_rules! list_for_each_owner {
    ( $var:ident, $container:ty, $field:ident, $head:expr, $body:block) => {{
        use $crate::list_owner;

        let mut _next = $head.head;
        while let Some(_node) = _next && _node != $head.into_node() {
            let mut $var = list_owner!(_node, $container, $field);
            $body
            _next = unsafe { _node.as_ref() }.next;
        }
    }};
}

#[macro_export]
macro_rules! list_for_each_owner_safe {
    ($var:ident, $next:ident, $container:ty, $field:ident, $head:expr, $body:block) => {{
        use $crate::list_owner;
        let mut _node = unsafe { (*$head).head };
        while !_node.is_none() && _node != unsafe { (*$head).tail } {
            let $next = unsafe { (*_node).next };
            let $var = list_owner!(_node, $container, $field);
            $body
            _node = $next;
        }
    }};
}

// 当目标支持宽原子并需要对齐时，通过 cfg_attr 在类型上强制对齐：
// - 在 64 位目标且支持 128-bit atomics 时，要求 16 字节对齐以匹配 AtomicU128。
// - 在 32 位目标且支持 64-bit atomics 时，要求 8 字节对齐以匹配 AtomicU64。
// 这样可以在编译期保证对齐，从而避免运行时的对齐检查和回退。
#[cfg_attr(
    all(target_has_atomic = "128", target_pointer_width = "64"),
    repr(align(16))
)]
#[cfg_attr(
    all(target_has_atomic = "64", target_pointer_width = "32"),
    repr(align(8))
)]
#[derive(PartialEq, Default, Debug)]
#[repr(C)]
pub struct ListHead<Owner> {
    pub tail: Option<NonNull<ListNode<Owner>>>,
    pub head: Option<NonNull<ListNode<Owner>>>,
}

impl<Owner> ListHead<Owner> {
    pub const fn empty() -> Self {
        Self {
            tail: None,
            head: None,
        }
    }

    #[inline(always)]
    pub fn init(&mut self) {
        let ptr = self.into_node();
        self.head = Some(ptr);
        self.tail = Some(ptr);
    }

    #[inline(always)]
    pub fn into_node(&self) -> NonNull<ListNode<Owner>> {
        NonNull::from_ref(self).cast()
    }

    #[inline(always)]
    pub fn add_head(&mut self, node: &mut ListNode<Owner>) {
        node.add(self.into_node(), self.head.unwrap());
    }

    #[inline(always)]
    pub fn add_tail(&mut self, node: &mut ListNode<Owner>) {
        node.add(self.tail.unwrap(), self.into_node());
    }

    #[inline(always)]
    pub fn is_empty(&self) -> bool {
        (self.head.is_some_and(|ptr| ptr == self.into_node()))
            || self.head.is_none()
            || self.tail.is_none()
    }
}

// 专门为包含 `ListHead` 的 `Spinlock` 提供的辅助方法：尝试在支持的
// 平台上使用一次宽原子加载来原子性地获取 ListHead 的快照。
// 如果目标平台不支持所需的原子宽度或者 `ListHead` 的对齐不满足要求，
// 则回落为加锁读取以保证一致性。
impl<Owner> Spinlock<ListHead<Owner>> {
    /// 尝试以一致的方式读取 `ListHead { head, tail }` 的快照，返回
    /// 一对 `Option<NonNull<ListNode<Owner>>>`（head, tail）。
    ///
    /// 成功时返回 `ListHead { head, tail }`快照（可能通过原子加载或通过加锁回退），
    /// 在走原子路径时本函数不会阻塞；否则会获取锁以安全读取。
    #[allow(unused)]
    pub fn get_atomic_snapshot(&mut self) -> ListHead<Owner> {
        // 注意：下面的导入放在 cfg 分支中，以避免在某些目标上出现
        // “未使用的导入”警告。
        // 64 位指针被打包为 128 位整数
        #[cfg(all(target_has_atomic = "128", target_pointer_width = "64"))]
        {
            use core::mem::transmute;
            use core::sync::atomic::AtomicU128;

            let ptr: *mut AtomicU128 = unsafe { transmute(self._inner.get()) };

            let v = unsafe { &*ptr }.load(core::sync::atomic::Ordering::Relaxed);

            return unsafe { transmute::<u128, ListHead<Owner>>(v) };
        }

        // 32 位指针被打包为 64-bit 位整数
        #[cfg(all(target_has_atomic = "64", target_pointer_width = "32"))]
        {
            use core::mem::transmute;
            use core::sync::atomic::AtomicU64;

            let ptr: *mut AtomicU64 = unsafe { transmute(self._inner.get()) };

            let v = unsafe { &*ptr }.load(core::sync::atomic::Ordering::Relaxed);

            return unsafe { transmute::<u64, ListHead<Owner>>(v) };
        }

        // 回退：对于不支持相应原子操作的平台，使用自旋锁来获取
        {
            let guard = self.lock();
            let snapshot = *guard; // ListHead is Copy

            snapshot
        }
    }

    #[allow(unused)]
    pub fn set_atomic(&mut self, new: ListHead<Owner>) {
        // 64 位指针被打包为 128 位整数（低 64 位存 head，高 64 位存 tail）
        #[cfg(all(target_has_atomic = "128", target_pointer_width = "64"))]
        {
            use core::mem::transmute;
            use core::sync::atomic::{AtomicU128, Ordering};

            let ptr: *mut AtomicU128 = unsafe { transmute(self._inner.get()) };
            let new: u128 = transmute(new);
            unsafe { &*ptr }.store(new, Ordering::Release);
            return;
        }

        // 32-bit pointers packed into 64-bit integer (low=head, high=tail)
        #[cfg(all(target_has_atomic = "64", target_pointer_width = "32"))]
        {
            use core::mem::transmute;
            use core::sync::atomic::{AtomicU64, Ordering};

            let ptr: *mut AtomicU64 = unsafe { transmute(self._inner.get()) };
            let new: u64 = unsafe { transmute(new) };
            unsafe { &*ptr }.store(new, Ordering::Release);
            return;
        }

        {
            let mut guard = self.lock();
            *guard = new;
        }
    }
}

// Manual impls to avoid requiring `Owner: Clone`/`Owner: Copy`.
// Raw pointers are Copy, so it's safe for the list pointer structs.
impl<Owner> Copy for ListHead<Owner> {}
impl<Owner> Clone for ListHead<Owner> {
    fn clone(&self) -> Self {
        *self
    }
}

#[cfg_attr(
    all(target_has_atomic = "128", target_pointer_width = "64"),
    repr(align(16))
)]
#[cfg_attr(
    all(target_has_atomic = "64", target_pointer_width = "32"),
    repr(align(8))
)]
#[derive(PartialEq, Default, Debug)]
#[repr(C, align(4))]
pub struct ListNode<Owner> {
    pub prev: Option<NonNull<ListNode<Owner>>>,
    pub next: Option<NonNull<ListNode<Owner>>>,
    _phantom: PhantomData<Owner>,
}

impl<Owner> ListNode<Owner> {
    pub const fn new() -> Self {
        Self {
            prev: None,
            next: None,
            _phantom: PhantomData,
        }
    }

    pub fn init(&mut self) {
        self.prev = None;
        self.next = None;
    }

    #[inline(always)]
    pub fn add(&mut self, mut prev: NonNull<ListNode<Owner>>, mut next: NonNull<ListNode<Owner>>) {
        let (_prev, _next) = unsafe { (prev.as_mut(), next.as_mut()) };
        let _self = Some(NonNull::from_mut(self));

        _next.prev = _self;
        self.next = Some(next);
        self.prev = Some(prev);
        _prev.next = _self;
    }

    #[inline(always)]
    pub fn add_after(&mut self, mut node: NonNull<ListNode<Owner>>) {
        let (_node, _next) = unsafe {
            let node = node.as_mut();
            let next = node
                .next
                .expect("Trying to add_after on a node with no next")
                .as_mut();
            (node, next)
        };
        let _self = Some(NonNull::from_mut(self));

        _next.prev = _self;

        self.prev = Some(node);
        self.next = _node.next;

        _node.next = _self;
    }

    #[inline(always)]
    pub fn add_before(&mut self, mut node: NonNull<ListNode<Owner>>) {
        let (_node, _prev) = unsafe {
            let node = node.as_mut();
            let prev = node
                .prev
                .expect("Trying to add_before on a node with no prev")
                .as_mut();
            (node, prev)
        };
        let _self = Some(NonNull::from_mut(self));

        _prev.next = _self;

        self.prev = _node.prev;
        self.next = Some(node);

        _node.prev = _self;
    }

    #[inline(always)]
    pub fn del(&mut self) {
        let prev = unsafe { self.prev.expect("prev node is null!").as_mut() };
        let next = unsafe { self.next.expect("next node is null!").as_mut() };

        prev.next = self.next;
        next.prev = self.prev;

        self.prev = None;
        self.next = None;
    }

    #[inline(always)]
    pub fn is_linked(&self) -> bool {
        self.prev.is_some() && self.next.is_some()
    }
}

impl<Owner> Copy for ListNode<Owner> {}
impl<Owner> Clone for ListNode<Owner> {
    fn clone(&self) -> Self {
        *self
    }
}
