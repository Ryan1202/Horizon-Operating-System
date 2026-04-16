// no external imports required

#[cfg(any(
    all(target_has_atomic = "128", target_pointer_width = "64"),
    all(target_has_atomic = "64", target_pointer_width = "32")
))]
use core::ops::{Deref, DerefMut};
use core::{
    marker::{PhantomData, PhantomPinned},
    mem::MaybeUninit,
    pin::Pin,
    ptr::NonNull,
};

#[macro_export]
macro_rules! list_owner {
    ($var:ident, $container:ty, $field:ident) => {{ $crate::container_of!($var.cast::<ListNode<$container>>(), $container, $field) }};
}

#[derive(PartialEq, Debug)]
pub struct Link<Owner> {
    prev: NonNull<Link<Owner>>,
    pub next: NonNull<Link<Owner>>,
    _phantom: (PhantomData<Owner>, PhantomPinned),
}

#[derive(Debug)]
#[repr(C)]
pub struct ListHead<Owner> {
    link: MaybeUninit<Link<Owner>>,
}

impl<T> Default for ListHead<T> {
    fn default() -> Self {
        Self::empty()
    }
}

impl<Owner> ListHead<Owner> {
    pub const fn empty() -> Self {
        Self {
            link: MaybeUninit::uninit(),
        }
    }

    #[inline(always)]
    pub fn init(self: &mut Pin<&mut Self>) {
        let ptr = self.as_ref().as_ptr();
        let link = Link {
            prev: ptr,
            next: ptr,
            _phantom: (PhantomData, PhantomPinned),
        };
        unsafe { self.as_mut().get_unchecked_mut().link.write(link) };
    }

    pub const fn as_ptr(self: &Pin<&Self>) -> NonNull<Link<Owner>> {
        NonNull::from_ref(self.get_ref()).cast()
    }

    #[inline(always)]
    pub fn add_head(self: &mut Pin<&mut Self>, node: Pin<&mut ListNode<Owner>>) {
        let prev = self.as_ref().as_ptr();
        let next = unsafe {
            self.as_mut()
                .get_unchecked_mut()
                .link
                .assume_init_mut()
                .next
        };
        unsafe { node.add(prev, next) };
    }

    #[inline(always)]
    pub fn add_tail(self: &mut Pin<&mut Self>, node: Pin<&mut ListNode<Owner>>) {
        let prev = unsafe {
            self.as_mut()
                .get_unchecked_mut()
                .link
                .assume_init_mut()
                .prev
        };
        let next = self.as_ref().as_ptr();
        unsafe { node.add(prev, next) };
    }

    #[inline(always)]
    pub fn del(self: &mut Pin<&mut Self>, node: Pin<&mut ListNode<Owner>>) {
        node.del();
    }

    #[inline(always)]
    pub fn is_empty(&self) -> bool {
        let ptr = NonNull::from_ref(self).cast();
        let link = unsafe { self.link.assume_init_ref() };
        link.prev == ptr && link.next == ptr
    }
}

pub struct ListIterator<Owner> {
    head: NonNull<Link<Owner>>,
    next: Option<NonNull<Link<Owner>>>,
    offset: isize,
    _phantom: PhantomData<Owner>,
}

impl<Owner> ListHead<Owner> {
    pub fn iter(self: Pin<&mut Self>, offset: usize) -> ListIterator<Owner> {
        let head = self.as_ref().as_ptr();

        let first = unsafe { NonNull::from_ref(self.link.assume_init_ref()) };
        ListIterator {
            head,
            next: if first != head { Some(first) } else { None },
            offset: -(offset as isize),
            _phantom: PhantomData,
        }
    }
}

impl<Owner> Iterator for ListIterator<Owner> {
    type Item = NonNull<Owner>;

    fn next(&mut self) -> Option<Self::Item> {
        let current = self.next;
        self.next = current
            .map(|current| unsafe { current.as_ref().next })
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

#[cfg(any(
    all(target_has_atomic = "128", target_pointer_width = "64"),
    all(target_has_atomic = "64", target_pointer_width = "32")
))]
impl<Owner> Deref for AtomicListNode<Owner> {
    type Target = ListNode<Owner>;

    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

#[cfg(any(
    all(target_has_atomic = "128", target_pointer_width = "64"),
    all(target_has_atomic = "64", target_pointer_width = "32")
))]
impl<Owner> DerefMut for AtomicListNode<Owner> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.inner
    }
}

#[cfg(any(
    all(target_has_atomic = "128", target_pointer_width = "64"),
    all(target_has_atomic = "64", target_pointer_width = "32")
))]
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
    pub link: Option<Link<Owner>>,
}

impl<Owner> ListNode<Owner> {
    pub const fn new() -> Self {
        Self { link: None }
    }

    pub fn init(self: Pin<&mut Self>) {
        // SAFETY: 只改链表指针，不移动节点
        let this = unsafe { self.get_unchecked_mut() };
        this.link = None;
    }

    #[inline(always)]
    const fn as_ptr(self: &Pin<&Self>) -> NonNull<Link<Owner>> {
        NonNull::from_ref(self.get_ref()).cast()
    }

    #[inline(always)]
    unsafe fn add(
        self: Pin<&mut Self>,
        mut prev: NonNull<Link<Owner>>,
        mut next: NonNull<Link<Owner>>,
    ) {
        let (_prev, _next) = unsafe { (prev.as_mut(), next.as_mut()) };
        let _self = self.as_ref().as_ptr();
        let self_ref = unsafe { self.get_unchecked_mut() };

        _next.prev = _self;
        _prev.next = _self;
        let link = Link {
            prev,
            next,
            _phantom: (PhantomData, PhantomPinned),
        };
        self_ref.link = Some(link);
    }

    /// 将当前节点添加到`node`节点之后
    #[inline(always)]
    pub fn add_after(self: Pin<&mut Self>, node: Pin<&mut Self>) {
        let prev = node.as_ref().as_ptr();

        unsafe {
            let node = node
                .get_unchecked_mut()
                .link
                .as_mut()
                .expect("Trying to add_after on an unlinked node!");

            let next = node.next;

            self.add(prev, next);
        }
    }

    /// 将当前节点添加到`node`节点之前
    #[inline(always)]
    pub fn add_before(self: Pin<&mut Self>, node: Pin<&mut Self>) {
        let next = node.as_ref().as_ptr();

        unsafe {
            let node = node
                .get_unchecked_mut()
                .link
                .as_mut()
                .expect("Trying to add_after on an unlinked node!");

            let prev = node.prev;

            self.add(prev, next);
        }
    }

    #[inline(always)]
    fn del(self: Pin<&mut Self>) {
        unsafe {
            let link = &mut self.get_unchecked_mut().link;

            let (mut prev, mut next) = {
                let link = link
                    .as_ref()
                    .expect("trying to delete a unlinked list node!");
                (link.prev, link.next)
            };

            prev.as_mut().next = next;
            next.as_mut().prev = prev;

            *link = None;
        }
    }

    #[inline(always)]
    pub fn is_linked(&self) -> bool {
        self.link.is_some()
    }
}
