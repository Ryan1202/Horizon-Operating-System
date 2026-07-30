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

#[repr(C)]
#[derive(PartialEq, Debug)]
pub struct Link<Owner> {
    prev: NonNull<Link<Owner>>,
    pub next: NonNull<Link<Owner>>,
    _phantom: (PhantomData<Owner>, PhantomPinned),
}

unsafe impl<Owner: Send> Send for Link<Owner> {}
unsafe impl<Owner: Sync> Sync for Link<Owner> {}

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
    pub unsafe fn init(&mut self) {
        let ptr = NonNull::from_ref(unsafe { self.link.assume_init_ref() });
        let link = Link {
            prev: ptr,
            next: ptr,
            _phantom: (PhantomData, PhantomPinned),
        };
        self.link.write(link);
    }

    #[inline(always)]
    pub fn init_pinned(self: Pin<&mut Self>) {
        unsafe { self.get_unchecked_mut().init() };
    }

    pub const fn as_ptr(&self) -> NonNull<Link<Owner>> {
        NonNull::from_ref(unsafe { self.link.assume_init_ref() }).cast()
    }

    #[inline(always)]
    pub fn add_head(&mut self, node: &mut ListNode<Owner>) {
        let prev = NonNull::from_ref(unsafe { self.link.assume_init_ref() });
        let next = unsafe { self.link.assume_init_mut().next };
        unsafe { node.add(prev, next) };
    }

    #[inline(always)]
    pub fn add_tail(&mut self, node: &mut ListNode<Owner>) {
        let prev = unsafe { self.link.assume_init_mut().prev };
        let next = NonNull::from_ref(unsafe { self.link.assume_init_ref() });
        unsafe { node.add(prev, next) };
    }

    #[inline(always)]
    pub fn delete(&mut self, node: &mut ListNode<Owner>) {
        node.delete();
    }

    #[inline(always)]
    pub fn delete_pinned(self: Pin<&mut Self>, node: &mut ListNode<Owner>) {
        unsafe { Pin::get_unchecked_mut(self) }.delete(node);
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
    pub fn iter(&self, offset: usize) -> ListIterator<Owner> {
        let head = unsafe { self.link.assume_init_ref() };
        let first = head.next;

        let head = NonNull::from_ref(head);

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
        current.map(|p| unsafe { p.byte_offset(self.offset).cast() })
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

    pub fn init(&mut self) {
        // SAFETY: 只改链表指针，不移动节点
        self.link = None;
    }

    #[inline(always)]
    const fn as_ptr(&self) -> NonNull<Link<Owner>> {
        NonNull::from_ref(self).cast()
    }

    #[inline(always)]
    unsafe fn add(&mut self, mut prev: NonNull<Link<Owner>>, mut next: NonNull<Link<Owner>>) {
        let (_prev, _next) = unsafe { (prev.as_mut(), next.as_mut()) };
        let _self = self.as_ptr();

        _next.prev = _self;
        _prev.next = _self;
        let link = Link {
            prev,
            next,
            _phantom: (PhantomData, PhantomPinned),
        };
        self.link = Some(link);
    }

    /// 将当前节点添加到`node`节点之后
    #[inline(always)]
    pub fn add_after(&mut self, node: &mut Self) {
        unsafe {
            let node = node
                .link
                .as_mut()
                .expect("Trying to add_after on an unlinked node!");

            let prev = NonNull::from_ref(node);

            let next = node.next;

            self.add(prev, next);
        }
    }

    /// 将当前节点添加到`node`节点之前
    #[inline(always)]
    pub fn add_before(&mut self, node: &mut Self) {
        unsafe {
            let node = node
                .link
                .as_mut()
                .expect("Trying to add_after on an unlinked node!");

            let next = NonNull::from_ref(node);

            let prev = node.prev;

            self.add(prev, next);
        }
    }

    #[inline(always)]
    fn delete(&mut self) {
        unsafe {
            let link = &mut self.link;

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
