use core::sync::atomic::{AtomicUsize, Ordering};

static NEXT_THREAD_ID: AtomicUsize = AtomicUsize::new(0);

#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(transparent)]
pub struct ThreadId(usize);

impl ThreadId {
    pub(super) fn new() -> Self {
        let id = NEXT_THREAD_ID.fetch_add(1, Ordering::Relaxed);

        assert_ne!(id, usize::MAX, "thread id overflow");

        Self(id)
    }
}
