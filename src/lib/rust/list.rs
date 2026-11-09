use core::{
    marker::{PhantomData, PhantomPinned},
    pin::Pin,
    ptr::NonNull,
};

mod thin;
mod utils;

pub use thin::{ThinListHead, ThinListNode};
pub use utils::ListMember;

#[derive(PartialEq, Debug)]
#[repr(C)]
pub struct Link<Owner: ?Sized> {
    prev: NonNull<Link<Owner>>,
    next: NonNull<Link<Owner>>,
    _phantom: (PhantomData<Owner>, PhantomPinned),
}

unsafe impl<Owner: Send> Send for Link<Owner> {}

impl<Owner> Link<Owner> {
    pub const fn new(prev: NonNull<Link<Owner>>, next: NonNull<Link<Owner>>) -> Self {
        Self {
            prev,
            next,
            _phantom: (PhantomData, PhantomPinned),
        }
    }
}

#[derive(Debug)]
#[repr(transparent)]
pub struct ListHead<Member: ListMember> {
    link: Link<Member::LinkOwner>,
}

unsafe impl<Owner: ListMember + Sync> Sync for ListHead<Owner> {}

const impl<T: ListMember> Default for ListHead<T> {
    fn default() -> Self {
        Self {
            link: Link::new(NonNull::dangling(), NonNull::dangling()),
        }
    }
}

impl<Member> ListHead<Member>
where
    Member: ListMember<Node = ListNode<<Member as ListMember>::LinkOwner>>,
{
    /// 创建一个新的 ListHead 实例
    ///
    /// # Safety
    ///
    /// `ptr` 必须是当前 ListHead 实例的指针
    pub const unsafe fn new(ptr: *const Self) -> Self {
        let link = NonNull::from_ref(unsafe { &(*ptr).link });

        Self {
            link: Link::new(link, link),
        }
    }

    #[inline(always)]
    pub unsafe fn init(mut self: Pin<&mut Self>) {
        let this = unsafe { self.as_mut().get_unchecked_mut() };
        let ptr = unsafe { NonNull::new_unchecked(&raw mut this.link) };

        this.link = Link::new(ptr, ptr);
    }

    #[inline(always)]
    pub fn add_head(mut self: Pin<&mut Self>, owner: &mut Member::Owner) {
        unsafe {
            let prev = NonNull::new_unchecked(&raw mut self.as_mut().get_unchecked_mut().link);
            let next = self.link.next;
            let mut node = Member::node_of(NonNull::from_mut(owner));
            node.as_mut().add(prev, next);
        }
    }

    #[inline(always)]
    pub unsafe fn add_head_ref(&mut self, owner: &Member::Owner) {
        unsafe {
            let prev = NonNull::new_unchecked(&raw mut self.link);
            let next = self.link.next;
            let mut node = Member::node_of(NonNull::from_ref(owner));
            node.as_mut().add(prev, next);
        }
    }

    #[inline(always)]
    pub fn add_tail(mut self: Pin<&mut Self>, owner: &mut Member::Owner) {
        unsafe {
            let prev = self.link.prev;
            let next = NonNull::new_unchecked(&raw mut self.as_mut().get_unchecked_mut().link);
            let mut node = Member::node_of(NonNull::from_mut(owner));
            node.as_mut().add(prev, next);
        }
    }

    #[inline(always)]
    pub unsafe fn add_tail_ref(&mut self, owner: &Member::Owner) {
        unsafe {
            let prev = self.link.prev;
            let next = NonNull::new_unchecked(&raw mut self.link);
            let mut node = Member::node_of(NonNull::from_ref(owner));
            node.as_mut().add(prev, next);
        }
    }

    #[inline(always)]
    pub unsafe fn delete(&mut self, owner: &mut Member::Owner) {
        unsafe {
            Member::node_of(NonNull::from_mut(owner)).as_mut().delete();
        }
    }

    #[inline(always)]
    pub unsafe fn delete_ref(&mut self, owner: &Member::Owner) {
        unsafe {
            Member::node_of(NonNull::from_ref(owner)).as_mut().delete();
        }
    }

    #[inline(always)]
    pub unsafe fn delete_pinned(self: Pin<&mut Self>, owner: &mut Member::Owner) {
        unsafe { Pin::get_unchecked_mut(self).delete(owner) };
    }

    #[inline(always)]
    pub unsafe fn delete_ref_pinned(self: Pin<&mut Self>, owner: &Member::Owner) {
        unsafe { Pin::get_unchecked_mut(self).delete_ref(owner) };
    }

    #[inline(always)]
    pub fn is_empty(&self) -> bool {
        debug_assert!(
            self.link.prev != NonNull::dangling() && self.link.next != NonNull::dangling(),
            "ListHead is not initialized"
        );
        let ptr = NonNull::from_ref(self).cast();

        self.link.prev == ptr && self.link.next == ptr
    }
}

pub struct ListIterator<'a, Member: ListMember> {
    head: NonNull<Link<Member::LinkOwner>>,
    next: Option<NonNull<Link<Member::LinkOwner>>>,
    _phantom: PhantomData<&'a ListHead<Member>>,
}

impl<Member: ListMember> ListHead<Member> {
    pub unsafe fn iter(&self) -> ListIterator<'_, Member> {
        debug_assert!(
            self.link.prev != NonNull::dangling() && self.link.next != NonNull::dangling(),
            "ListHead is not initialized"
        );

        let first = self.link.next;

        let head = NonNull::from_ref(&self.link);

        ListIterator {
            head,
            next: if first != head { Some(first) } else { None },
            _phantom: PhantomData,
        }
    }
}

impl<Member: ListMember> Iterator for ListIterator<'_, Member> {
    type Item = NonNull<Member::Owner>;

    fn next(&mut self) -> Option<Self::Item> {
        let current = self.next;
        self.next = current
            .map(|current| unsafe { current.as_ref().next })
            // 判断是否到达尾节点
            .and_then(|next| (next != self.head).then_some(next));

        // 转换到 Owner 类型
        current
            .map(|p| p.cast())
            .map(|p| unsafe { Member::owner_of(p) })
    }
}

#[derive(PartialEq, Default, Debug)]
#[repr(transparent)]
pub struct ListNode<Owner: ?Sized> {
    link: Option<Link<Owner>>,
}

unsafe impl<Owner> Sync for ListNode<Owner> {}

impl<Owner> ListNode<Owner> {
    pub const fn new() -> Self {
        Self { link: None }
    }

    /// 将当前节点添加到`prev`和`next`之间
    ///
    /// # Safety
    ///
    /// 当前节点的`link`必须为`None`，否则会导致链表结构损坏
    #[inline(always)]
    unsafe fn add(&mut self, mut prev: NonNull<Link<Owner>>, mut next: NonNull<Link<Owner>>) {
        assert!(self.link.is_none(), "trying to add a linked list node");
        self.link = Some(Link::new(prev, next));

        unsafe {
            let _self = NonNull::new_unchecked(self.link.as_mut().unwrap());
            prev.as_mut().next = _self;
            next.as_mut().prev = _self;
        };
    }

    /// 将当前节点添加到`node`节点之后
    #[inline(always)]
    pub unsafe fn add_after(&mut self, node: &mut Self) {
        unsafe {
            let node = node
                .link
                .as_mut()
                .expect("trying to add_after an unlinked node");

            let next = node.next;
            let prev = NonNull::from_mut(node);

            self.add(prev, next);
        }
    }

    /// 将当前节点添加到`node`节点之前
    #[inline(always)]
    pub unsafe fn add_before(&mut self, node: &mut Self) {
        unsafe {
            let node = node
                .link
                .as_mut()
                .expect("trying to add_before an unlinked node");

            let prev = node.prev;
            let next = NonNull::from_mut(node);

            self.add(prev, next);
        }
    }

    #[inline(always)]
    unsafe fn delete(&mut self) {
        unsafe {
            let link = self
                .link
                .as_mut()
                .expect("trying to delete an unlinked list node");

            link.prev.as_mut().next = link.next;
            link.next.as_mut().prev = link.prev;

            self.link = None;
        }
    }

    #[inline(always)]
    pub fn is_linked(&self) -> bool {
        self.link.is_some()
    }

    pub(crate) fn next(&self) -> Option<NonNull<Self>> {
        self.link.as_ref().map(|link| link.next.cast())
    }
}
