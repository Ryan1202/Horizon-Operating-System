// no external imports required

#[cfg(any(
    all(target_has_atomic = "128", target_pointer_width = "64"),
    all(target_has_atomic = "64", target_pointer_width = "32")
))]
use core::{
    marker::{PhantomData, PhantomPinned},
    ops::{Deref, DerefMut},
    pin::Pin,
    ptr::NonNull,
};

#[macro_export]
macro_rules! list_owner {
    ($var:ident, $container:ty, $field:ident) => {{ $crate::container_of!($var.cast::<ListNode<$container>>(), $container, $field) }};
}

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
    pub fn init(self: &mut Pin<&mut Self>) {
        let ptr = self.as_ref().into_node();
        let self_ref = unsafe { self.as_mut().get_unchecked_mut() };
        self_ref.head = Some(ptr);
        self_ref.tail = Some(ptr);
    }

    pub const fn into_node(self: Pin<&Self>) -> NonNull<ListNode<Owner>> {
        NonNull::from_ref(self.get_ref()).cast()
    }

    #[inline(always)]
    pub fn add_head(self: &mut Pin<&mut Self>, node: Pin<&mut ListNode<Owner>>) {
        let prev = self.as_ref().into_node();
        let next = self.head.unwrap();
        unsafe { node.add(prev, next) };
    }

    #[inline(always)]
    pub fn add_tail(self: &mut Pin<&mut Self>, node: Pin<&mut ListNode<Owner>>) {
        let prev = self.tail.unwrap();
        let next = self.as_ref().into_node();
        unsafe { node.add(prev, next) };
    }

    #[inline(always)]
    pub fn is_empty(&self) -> bool {
        (self
            .head
            .is_some_and(|ptr| ptr == unsafe { Pin::new_unchecked(self) }.into_node()))
            || self.head.is_none()
            || self.tail.is_none()
    }
}

pub struct ListIterator<Owner> {
    head: NonNull<ListNode<Owner>>,
    next: Option<NonNull<ListNode<Owner>>>,
    offset: isize,
    _phantom: PhantomData<Owner>,
}

impl<Owner> ListHead<Owner> {
    pub fn iter(&mut self, offset: usize) -> ListIterator<Owner> {
        let head = unsafe { Pin::new_unchecked(&*self).into_node() };
        ListIterator {
            head,
            next: self
                .head
                .and_then(|first| if first != head { Some(first) } else { None }),
            offset: -(offset as isize),
            _phantom: PhantomData,
        }
    }
}

impl<Owner> Iterator for ListIterator<Owner> {
    type Item = NonNull<Owner>;

    fn next(&mut self) -> Option<Self::Item> {
        let current = self.next;
        self.next = self
            .next
            // 获取下一个节点
            .and_then(|current| unsafe { current.as_ref().next })
            // 判断是否到达尾节点
            .and_then(|next| (next != self.head).then_some(next));

        // 转换到 Owner 类型
        current.map(|p| unsafe { p.offset(self.offset).cast() })
    }
}

#[cfg(any(
    all(target_has_atomic = "128", target_pointer_width = "64"),
    all(target_has_atomic = "64", target_pointer_width = "32")
))]
#[cfg_attr(
    all(target_has_atomic = "128", target_pointer_width = "64"),
    repr(align(16))
)]
#[cfg_attr(
    all(target_has_atomic = "64", target_pointer_width = "32"),
    repr(align(8))
)]
pub struct AtomicListNode<Owner> {
    inner: ListNode<Owner>,
}

impl<Owner> Deref for AtomicListNode<Owner> {
    type Target = ListNode<Owner>;

    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

impl<Owner> DerefMut for AtomicListNode<Owner> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.inner
    }
}

impl<Owner> AtomicListNode<Owner> {
    /// 尝试以一致的方式读取 `ListNode { head, tail }` 的快照，返回
    /// 一对 `Option<NonNull<ListNode<Owner>>>`（head, tail）。
    ///
    /// 成功时返回 `ListNode { head, tail }`快照（可能通过原子加载或通过加锁回退），
    /// 在走原子路径时本函数不会阻塞；否则会获取锁以安全读取。
    #[allow(unused)]
    fn get_atomic_snapshot(&mut self) -> ListNode<Owner> {
        // 注意：下面的导入放在 cfg 分支中，以避免在某些目标上出现
        // “未使用的导入”警告。
        // 64 位指针被打包为 128 位整数
        #[cfg(all(target_has_atomic = "128", target_pointer_width = "64"))]
        {
            use core::mem::transmute;
            use core::sync::atomic::AtomicU128;

            let ptr: *mut AtomicU128 = unsafe { transmute(self.inner.get()) };

            let v = unsafe { &*ptr }.load(core::sync::atomic::Ordering::Relaxed);

            return unsafe { transmute::<u128, ListNode<Owner>>(v) };
        }

        // 32 位指针被打包为 64-bit 位整数
        #[cfg(all(target_has_atomic = "64", target_pointer_width = "32"))]
        {
            use core::mem::transmute;
            use core::sync::atomic::AtomicU64;

            let ptr: *mut AtomicU64 = self.deref_mut() as *mut ListNode<Owner> as *mut AtomicU64;

            let v = unsafe { &*ptr }.load(core::sync::atomic::Ordering::Relaxed);

            unsafe { transmute::<u64, ListNode<Owner>>(v) }
        }
    }

    #[allow(unused)]
    fn set_atomic(&mut self, new: ListHead<Owner>) {
        // 64 位指针被打包为 128 位整数（低 64 位存 head，高 64 位存 tail）
        #[cfg(all(target_has_atomic = "128", target_pointer_width = "64"))]
        {
            use core::mem::transmute;
            use core::sync::atomic::{AtomicU128, Ordering};

            let ptr: *mut AtomicU128 = unsafe { transmute(self.inner.get()) };
            let new: u128 = transmute(new);
            unsafe { &*ptr }.store(new, Ordering::Release);
            return;
        }

        // 32-bit pointers packed into 64-bit integer (low=head, high=tail)
        #[cfg(all(target_has_atomic = "64", target_pointer_width = "32"))]
        {
            use core::mem::transmute;
            use core::sync::atomic::{AtomicU64, Ordering};

            let ptr: *mut AtomicU64 = self.deref_mut() as *mut ListNode<Owner> as *mut AtomicU64;
            let new: u64 = unsafe { transmute(new) };
            unsafe { &*ptr }.store(new, Ordering::Release);
        }
    }
}

#[derive(PartialEq, Default, Debug)]
#[repr(C)]
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

    pub fn init(self: Pin<&mut Self>) {
        // SAFETY: 只改链表指针，不移动节点
        let this = unsafe { self.get_unchecked_mut() };
        this.prev = None;
        this.next = None;
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
    pub fn del(self: Pin<&mut Self>, _head: &mut ListHead<Owner>) {
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
