// no external imports required

#[macro_export]
macro_rules! list_owner {
    ($var:ident, $container:ty, $field:ident) => {
        unsafe { $crate::container_of!($var, $container, $field) }
    };
}

#[macro_export]
macro_rules! list_first_owner {
    ($container:ty, $field:ident, $head:expr) => {{
        use $crate::list_owner;
        unsafe { $head.head.as_mut() }.and_then(|n| Some(list_owner!(n, $container, $field)))
    }};
}

#[macro_export]
macro_rules! list_for_each_owner {
    ( $var:ident, $container:ty, $field:ident, $head:expr, $body:block) => {{
        use $crate::list_owner;
        let mut _node = $head.head;
        while !_node.is_null() && _node != unsafe { $head.into_node() } {
            let $var = list_owner!(_node, $container, $field);
            $body
            _node = unsafe { (*_node).next };
        }
    }};
}

#[macro_export]
macro_rules! list_for_each_owner_safe {
    ($var:ident, $next:ident, $container:ty, $field:ident, $head:expr, $body:block) => {{
        use $crate::list_owner;
        let mut _node = unsafe { (*$head).head };
        while !_node.is_null() && _node != unsafe { (*$head).tail } {
            let $next = unsafe { (*_node).next };
            let $var = list_owner!(_node, $container, $field);
            $body
            _node = $next;
        }
    }};
}

#[repr(C)]
pub struct ListHead<Owner> {
    pub tail: *mut ListNode<Owner>,
    pub head: *mut ListNode<Owner>,
}

impl<Owner> ListHead<Owner> {
    pub const fn empty() -> Self {
        Self {
            tail: core::ptr::null_mut(),
            head: core::ptr::null_mut(),
        }
    }

    #[inline(always)]
    pub fn init(&mut self) {
        let ptr = unsafe { self.into_node() };
        self.head = ptr;
        self.tail = ptr;
    }

    #[inline(always)]
    pub unsafe fn into_node(&mut self) -> *mut ListNode<Owner> {
        self as *mut ListHead<Owner> as *mut ListNode<Owner>
    }

    #[inline(always)]
    pub unsafe fn add_head(&mut self, node: &mut ListNode<Owner>) {
        node.add(self.into_node(), self.head);
    }

    #[inline(always)]
    pub unsafe fn add_tail(&mut self, node: &mut ListNode<Owner>) {
        node.add(self.tail, self.into_node());
    }

    #[inline(always)]
    pub fn is_empty(&self) -> bool {
        self.head == self.tail || self.head.is_null() || self.tail.is_null()
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

#[repr(C)]
pub struct ListNode<Owner> {
    pub prev: *mut ListNode<Owner>,
    pub next: *mut ListNode<Owner>,
}

impl<Owner> ListNode<Owner> {
    pub const fn new() -> Self {
        Self {
            prev: core::ptr::null_mut(),
            next: core::ptr::null_mut(),
        }
    }

    pub fn init(&mut self) {
        self.prev = core::ptr::null_mut();
        self.next = core::ptr::null_mut();
    }

    #[inline(always)]
    pub unsafe fn add(&mut self, prev: *mut ListNode<Owner>, next: *mut ListNode<Owner>) {
        let prev = prev.as_mut().unwrap_unchecked();
        let next = next.as_mut().unwrap_unchecked();

        next.prev = self as *mut ListNode<Owner>;
        self.next = next as *mut ListNode<Owner>;

        self.prev = prev as *mut ListNode<Owner>;
        prev.next = self as *mut ListNode<Owner>;
    }

    #[inline(always)]
    pub unsafe fn add_after(&mut self, node: *mut ListNode<Owner>) {
        let node = node.as_mut().unwrap();
        let node_next = node.next.as_mut().unwrap();
        node_next.prev = self as *mut ListNode<Owner>;

        self.prev = node;
        self.next = node.next;

        node.next = self as *mut ListNode<Owner>;
    }

    #[inline(always)]
    pub unsafe fn add_before(&mut self, node: *mut ListNode<Owner>) {
        let node = node.as_mut().unwrap();
        let node_prev = node.prev.as_mut().unwrap();
        node_prev.next = self as *mut ListNode<Owner>;

        node.prev = self.prev;
        node.next = self as *mut ListNode<Owner>;

        node.prev = self as *mut ListNode<Owner>;
    }

    #[inline(always)]
    pub unsafe fn del(&mut self) {
        let prev = self.prev.as_mut().unwrap();
        let next = self.next.as_mut().unwrap();

        prev.next = self.next;
        next.prev = self.prev;

        self.prev = core::ptr::null_mut();
        self.next = core::ptr::null_mut();
    }

    #[inline(always)]
    pub fn is_linked(&self) -> bool {
        !self.prev.is_null() && !self.next.is_null()
    }
}

impl<Owner> Copy for ListNode<Owner> {}
impl<Owner> Clone for ListNode<Owner> {
    fn clone(&self) -> Self {
        *self
    }
}
