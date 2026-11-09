use core::{
    marker::{PhantomData, PhantomPinned},
    pin::Pin,
    ptr::NonNull,
};

use crate::lib::rust::list::ListMember;

type Next<Owner> = Option<NonNull<ThinListNode<Owner>>>;
type PrevPointer<Owner> = NonNull<Next<Owner>>;
type Node<Member> = ThinListNode<<Member as ListMember>::LinkOwner>;

#[derive(PartialEq, Debug)]
#[repr(C)]
struct ThinLink<Owner: ?Sized> {
    /// 指向下一节点的指针
    next: Next<Owner>,
    /// 上一个节点中指向这一节点的字段的指针
    ///
    /// 由于 [`ThinListHead`] 和 [`ThinListNode`] 内存布局完全不同，
    /// 所以只有指针字段的指针是可以通用的
    pprev: PrevPointer<Owner>,
    _phantom: (PhantomData<Owner>, PhantomPinned),
}
unsafe impl<Owner: Send> Send for ThinLink<Owner> {}

impl<Owner> ThinLink<Owner> {
    pub const fn new(pprev: PrevPointer<Owner>, next: Next<Owner>) -> Self {
        Self {
            pprev,
            next,
            _phantom: (PhantomData, PhantomPinned),
        }
    }
}

#[derive(Debug)]
#[repr(transparent)]
pub struct ThinListHead<Member: ListMember> {
    /// 指向第一个节点的指针
    first: Next<Member::LinkOwner>,
}

unsafe impl<Owner: ListMember + Sync> Sync for ThinListHead<Owner> {}

const impl<T: ListMember<Node = Node<T>>> Default for ThinListHead<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<Member: ListMember<Node = Node<Member>>> ThinListHead<Member> {
    pub const fn new() -> Self {
        Self { first: None }
    }

    const fn pprev(&self) -> PrevPointer<Member::LinkOwner> {
        NonNull::from_ref(&self.first)
    }

    #[inline(always)]
    pub fn add_head(self: Pin<&mut Self>, owner: &mut Member::Owner) {
        unsafe {
            let mut node = Member::node_of(NonNull::from_mut(owner));
            node.as_mut().add_after(self.pprev());
        }
    }

    #[inline(always)]
    pub unsafe fn add_head_ref(&mut self, owner: &Member::Owner) {
        unsafe {
            let mut node = Member::node_of(NonNull::from_ref(owner));
            node.as_mut().add_after(self.pprev());
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
        self.first.is_none()
    }
}

#[derive(PartialEq, Default, Debug)]
#[repr(transparent)]
pub struct ThinListNode<Owner: ?Sized> {
    link: Option<ThinLink<Owner>>,
}

unsafe impl<Owner> Sync for ThinListNode<Owner> {}

impl<Owner> ThinListNode<Owner> {
    pub const fn new() -> Self {
        Self { link: None }
    }

    /// 将当前节点添加到前一节点之后
    ///
    /// `pprev` 是前一节点指向下一节点的字段的指针
    #[inline(always)]
    pub unsafe fn add_after(&mut self, mut pprev: PrevPointer<Owner>) {
        assert!(self.link.is_none(), "trying add to a linked list node");

        unsafe {
            let _self = NonNull::from_ref(self);
            let next = pprev.as_mut().replace(_self);

            self.link = Some(ThinLink::new(pprev, next));

            if let Some(mut next) = next {
                let link = self.link.as_ref().unwrap_unchecked();
                next.as_mut()
                    .link
                    .as_mut()
                    .expect("prev node linked to an unlinked node")
                    .pprev = NonNull::from_ref(&link.next);
            }
        };
    }

    /// 将当前节点添加到`node`节点之前
    #[inline(always)]
    pub unsafe fn add_before(&mut self, node: &mut Self) {
        assert!(self.link.is_none(), "trying add to a linked list node");

        unsafe {
            let _self = NonNull::from_ref(self);
            let next = Some(NonNull::from_ref(node));

            let next_link = node
                .link
                .as_mut()
                .expect("trying add to a unlinked list node");
            let mut pprev = next_link.pprev;

            self.link = Some(ThinLink::new(pprev, next));
            *pprev.as_mut() = Some(_self);

            let link = self.link.as_ref().unwrap_unchecked();
            next_link.pprev = NonNull::from_ref(&link.next);
        };
    }

    #[inline(always)]
    unsafe fn delete(&mut self) {
        unsafe {
            let link = self
                .link
                .as_mut()
                .expect("trying to delete an unlinked list node");

            if let Some(mut next) = link.next {
                next.as_mut()
                    .link
                    .as_mut()
                    .expect("next node is unlinked")
                    .pprev = link.pprev;
            }

            *link.pprev.as_mut() = link.next;

            self.link = None;
        }
    }

    #[inline(always)]
    pub fn is_linked(&self) -> bool {
        self.link.is_some()
    }

    pub(crate) fn next(&self) -> Option<NonNull<Self>> {
        self.link.as_ref().and_then(|link| link.next)
    }
}
