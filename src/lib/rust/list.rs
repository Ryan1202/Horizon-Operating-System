// no external imports required

use core::{
    marker::{PhantomData, PhantomPinned},
    pin::Pin,
    ptr::NonNull,
};

use super::spinlock::Spinlock;

#[macro_export]
macro_rules! list_owner {
    ($var:ident, $container:ty, $field:ident) => {{ $crate::container_of!($var.cast::<ListNode<$container>>(), $container, $field) }};
}

#[macro_export]
macro_rules! list_first_owner {
    ($container:ty, $field:ident, $head:expr) => {{
        use $crate::list_owner;
        $head.head.map(|n| list_owner!(n, $container, $field))
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
#[derive(PartialEq, Debug)]
#[repr(C)]
pub struct ListHead<Owner> {
    pub tail: Option<NonNull<ListNode<Owner>>>,
    pub head: Option<NonNull<ListNode<Owner>>>,
    _phantom: PhantomPinned,
}

impl<T> Default for ListHead<T> {
    fn default() -> Self {
        Self::empty()
    }
}

impl<Owner> ListHead<Owner> {
    pub const fn empty() -> Self {
        Self {
            tail: None,
            head: None,
            _phantom: PhantomPinned,
        }
    }

    #[inline(always)]
    pub fn init(self: Pin<&mut Self>) {
        let ptr = self.as_ref().into_node();
        let self_ref = unsafe { self.get_unchecked_mut() };
        self_ref.head = Some(ptr);
        self_ref.tail = Some(ptr);
    }

    #[inline(always)]
    pub fn into_node(self: Pin<&Self>) -> NonNull<ListNode<Owner>> {
        NonNull::from_ref(self.get_ref()).cast()
    }

    #[inline(always)]
    pub fn add_head(self: Pin<&mut Self>, node: Pin<&mut ListNode<Owner>>) {
        let prev = self.as_ref().into_node();
        let next = self.head.unwrap();
        unsafe { node.add(prev, next) };
    }

    #[inline(always)]
    pub fn add_tail(self: Pin<&mut Self>, node: Pin<&mut ListNode<Owner>>) {
        let prev = self.tail.unwrap();
        let next = self.as_ref().into_node();
        unsafe { node.add(prev, next) };
    }

    #[inline(always)]
    pub fn is_empty(self: &Self) -> bool {
        (self
            .head
            .is_some_and(|ptr| ptr == unsafe { Pin::new_unchecked(self) }.into_node()))
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
    _phantom: (PhantomData<Owner>, PhantomPinned),
}

impl<Owner> ListNode<Owner> {
    pub const fn new() -> Self {
        Self {
            prev: None,
            next: None,
            _phantom: (PhantomData, PhantomPinned),
        }
    }

    pub fn init(&mut self) {
        self.prev = None;
        self.next = None;
    }

    #[inline(always)]
    fn ptr(&self) -> NonNull<Self> {
        NonNull::from_ref(self)
    }

    #[inline(always)]
    unsafe fn add(self: Pin<&mut Self>, mut prev: NonNull<Self>, mut next: NonNull<Self>) {
        let (_prev, _next) = unsafe { (prev.as_mut(), next.as_mut()) };
        let _self = Some(NonNull::from(self.as_ref().get_ref()));
        let self_ref = unsafe { self.get_unchecked_mut() };

        _next.prev = _self;
        _prev.next = _self;
        self_ref.next = Some(next);
        self_ref.prev = Some(prev);
    }

    /// 将当前节点添加到`node`节点之后
    #[inline(always)]
    pub fn add_after(self: Pin<&mut Self>, node: Pin<&mut Self>) {
        unsafe {
            let next = node
                .next
                .expect("Trying to add_after on a node with no next")
                .as_ref();

            self.add(node.ptr(), next.ptr());
        }
    }

    /// 将当前节点添加到`node`节点之前
    #[inline(always)]
    pub fn add_before(self: Pin<&mut Self>, node: Pin<&mut Self>) {
        unsafe {
            let prev = node
                .prev
                .expect("Trying to add_before on a node with no prev")
                .as_ref();

            self.add(prev.ptr(), node.ptr());
        }
    }

    #[inline(always)]
    pub fn del(self: Pin<&mut Self>) {
        let this = unsafe { self.get_unchecked_mut() };
        let prev = unsafe { this.prev.expect("prev node is null!").as_mut() };
        let next = unsafe { this.next.expect("next node is null!").as_mut() };

        prev.next = this.next;
        next.prev = this.prev;

        this.prev = None;
        this.next = None;
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
