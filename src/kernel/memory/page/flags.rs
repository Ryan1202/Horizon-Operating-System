use crate::kernel::memory::PageCacheType;

#[derive(Debug, Clone, Copy)]
pub struct PageFlags {
    pub present: bool,
    pub writable: bool,
    pub user: bool,
    pub no_execute: bool,
    pub global: bool,
    pub accessed: bool,
    pub dirty: bool,
    pub huge_page: bool,

    pub cache_type: PageCacheType,
}

impl PageFlags {
    pub const fn new() -> Self {
        Self {
            present: true,
            writable: true,
            user: false,
            no_execute: false,
            global: false,
            accessed: false,
            dirty: false,
            huge_page: true,

            cache_type: PageCacheType::WriteBack,
        }
    }

    pub const fn present(mut self, present: bool) -> Self {
        self.present = present;
        self
    }

    pub const fn writable(mut self, writable: bool) -> Self {
        self.writable = writable;
        self
    }

    pub const fn user(mut self, user: bool) -> Self {
        self.user = user;
        self
    }

    pub const fn executable(mut self, executable: bool) -> Self {
        self.no_execute = !executable;
        self
    }

    pub const fn global(mut self, global: bool) -> Self {
        self.global = global;
        self
    }

    pub const fn accessed(mut self, accessed: bool) -> Self {
        self.accessed = accessed;
        self
    }

    pub const fn dirty(mut self, dirty: bool) -> Self {
        self.dirty = dirty;
        self
    }

    pub const fn huge_page(mut self, huge_page: bool) -> Self {
        self.huge_page = huge_page;
        self
    }

    pub const fn cache_type(mut self, cache_type: PageCacheType) -> Self {
        self.cache_type = cache_type;
        self
    }
}

impl Default for PageFlags {
    fn default() -> Self {
        Self::new()
    }
}
