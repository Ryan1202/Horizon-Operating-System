use core::{
    cell::{SyncUnsafeCell, UnsafeCell},
    ptr::NonNull,
};

use crate::lib::rust::{
    field::FieldPath,
    list::{ListNode, thin::ThinListNode},
};

#[macro_export]
macro_rules! list_owner {
    ($var:ident, $container:ty, $field:ident) => {{ $crate::container_of!($var.cast::<ListNode<$container>>(), $container, $field) }};
}

pub const unsafe trait ListMember: FieldPath {
    type Owner;
    type LinkOwner;
    type Node;

    unsafe fn node_of(owner: NonNull<Self::Owner>) -> NonNull<Self::Node>;
    unsafe fn owner_of(node: NonNull<Self::Node>) -> NonNull<Self::Owner>;
}

pub const unsafe trait ListNodeStorage {
    type LinkOwner;
    type Node;

    unsafe fn node_ptr(storage: NonNull<Self>) -> NonNull<Self::Node>;
    unsafe fn storage_ptr(node: NonNull<Self::Node>) -> NonNull<Self>;
}

const unsafe impl<Owner> ListNodeStorage for ListNode<Owner> {
    type LinkOwner = Owner;
    type Node = ListNode<Self::LinkOwner>;

    unsafe fn node_ptr(storage: NonNull<Self>) -> NonNull<Self::Node> {
        storage
    }

    unsafe fn storage_ptr(node: NonNull<Self::Node>) -> NonNull<Self> {
        node
    }
}

const unsafe impl<Owner> ListNodeStorage for ThinListNode<Owner> {
    type LinkOwner = Owner;
    type Node = ThinListNode<Self::LinkOwner>;

    unsafe fn node_ptr(storage: NonNull<Self>) -> NonNull<Self::Node> {
        storage
    }

    unsafe fn storage_ptr(node: NonNull<Self::Node>) -> NonNull<Self> {
        node
    }
}

const unsafe impl<Owner> ListNodeStorage for UnsafeCell<ListNode<Owner>> {
    type LinkOwner = Owner;
    type Node = ListNode<Self::LinkOwner>;

    unsafe fn node_ptr(storage: NonNull<Self>) -> NonNull<Self::Node> {
        storage.cast()
    }

    unsafe fn storage_ptr(node: NonNull<Self::Node>) -> NonNull<Self> {
        node.cast()
    }
}

const unsafe impl<Owner> ListNodeStorage for UnsafeCell<ThinListNode<Owner>> {
    type LinkOwner = Owner;
    type Node = ThinListNode<Self::LinkOwner>;

    unsafe fn node_ptr(storage: NonNull<Self>) -> NonNull<Self::Node> {
        storage.cast()
    }

    unsafe fn storage_ptr(node: NonNull<Self::Node>) -> NonNull<Self> {
        node.cast()
    }
}

const unsafe impl<Owner> ListNodeStorage for SyncUnsafeCell<ListNode<Owner>> {
    type LinkOwner = Owner;
    type Node = ListNode<Self::LinkOwner>;

    unsafe fn node_ptr(storage: NonNull<Self>) -> NonNull<Self::Node> {
        storage.cast()
    }

    unsafe fn storage_ptr(node: NonNull<Self::Node>) -> NonNull<Self> {
        node.cast()
    }
}

const unsafe impl<Owner> ListNodeStorage for SyncUnsafeCell<ThinListNode<Owner>> {
    type LinkOwner = Owner;
    type Node = ThinListNode<Self::LinkOwner>;

    unsafe fn node_ptr(storage: NonNull<Self>) -> NonNull<Self::Node> {
        storage.cast()
    }

    unsafe fn storage_ptr(node: NonNull<Self::Node>) -> NonNull<Self> {
        node.cast()
    }
}

const unsafe impl<P> ListMember for P
where
    P: const FieldPath,
    P::Target: const ListNodeStorage,
{
    type Owner = P::Base;
    type LinkOwner = <P::Target as ListNodeStorage>::LinkOwner;
    type Node = <P::Target as ListNodeStorage>::Node;

    unsafe fn node_of(owner: NonNull<Self::Owner>) -> NonNull<Self::Node> {
        let storage = unsafe { P::project(owner) };
        unsafe { <P::Target as ListNodeStorage>::node_ptr(storage) }
    }

    unsafe fn owner_of(node: NonNull<Self::Node>) -> NonNull<Self::Owner> {
        let storage = unsafe { <P::Target as ListNodeStorage>::storage_ptr(node) };
        unsafe { P::unproject(storage) }
    }
}
