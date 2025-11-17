//! Rust implementation of kernel spinlocks exported for C usage.
//!
//! This provides C-callable functions that mirror the inline functions in
//! `src/include/kernel/spinlock.h`. The implementation operates on the
//! underlying integer lock word so it works for both the simple `volatile int`
//! variant and the debug `struct { int lock; ... }` variant (both have the
//! lock word at the start).

use core::cell::UnsafeCell;
use core::ffi::c_int;
use core::hint::spin_loop;
use core::ops::{Deref, DerefMut};
use core::ptr::NonNull;
use core::sync::atomic::{AtomicBool, Ordering};

extern "C" {
    // Architecture-specific helpers provided elsewhere (C/ASM).
    fn save_eflags_cli() -> c_int;
    fn io_store_eflags(flags: c_int);
}
pub struct CSpinlock {
    ptr: NonNull<SpinlockRaw>,
}

impl CSpinlock {
    pub unsafe fn from_ptr(ptr: *mut CSpinlock) -> Option<Self> {
        NonNull::new(ptr).map(|nn_ptr| CSpinlock { ptr: nn_ptr.cast() })
    }
}

impl Deref for CSpinlock {
    type Target = SpinlockRaw;

    fn deref(&self) -> &Self::Target {
        unsafe { self.ptr.as_ref() }
    }
}

/// A Rust-native spinlock abstraction. It does not expose its fields to C;
/// only the lock word (first 32-bit integer) is layout-compatible with the
/// C `spinlock_t` definition. C code that treats the lock as an `int` or a
/// struct whose first field is the lock word will be compatible.
#[repr(transparent)]
pub struct SpinlockRaw {
    // lock word must be first to preserve C compatibility
    lock: AtomicBool,
}

impl SpinlockRaw {
    /// Create a new unlocked spinlock value (suitable for static init via
    /// `Spinlock { lock: 0 }`).
    pub const fn new_unlocked() -> Self {
        SpinlockRaw {
            lock: AtomicBool::new(false),
        }
    }

    pub const fn new_locked() -> Self {
        SpinlockRaw {
            lock: AtomicBool::new(true),
        }
    }

    #[inline]
    pub fn try_lock(&mut self) -> c_int {
        match self
            .lock
            .compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed)
        {
            Ok(_) => 1,
            Err(_) => 0,
        }
    }

    #[inline]
    pub fn lock(&mut self) {
        while self
            .lock
            .compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed)
            .is_err()
        {
            while self.lock.load(Ordering::Relaxed) != false {
                spin_loop();
            }
        }
    }

    #[inline]
    pub fn unlock(&mut self) {
        self.lock.store(false, Ordering::Release);
    }

    #[inline]
    pub fn lock_irqsave(&mut self) -> c_int {
        let flags = unsafe { save_eflags_cli() };
        self.lock();
        flags
    }

    #[inline]
    pub fn try_lock_irqsave(&mut self) -> c_int {
        let flags = unsafe { save_eflags_cli() };
        let ok = self.try_lock();
        if ok != 0 {
            flags
        } else {
            unsafe { io_store_eflags(flags) };
            0
        }
    }

    #[inline]
    pub fn unlock_irqrestore(&mut self, flags: c_int) {
        self.unlock();
        unsafe { io_store_eflags(flags) };
    }
}

unsafe impl Sync for SpinlockRaw {}

pub struct Spinlock<T> {
    lock: SpinlockRaw,
    _inner: UnsafeCell<T>,
}

unsafe impl<T> Sync for Spinlock<T> where T: Send {}

pub struct SpinGuard<'a, T> {
    lock: &'a mut SpinlockRaw,
    _inner: &'a mut T,
}

pub struct SpinIrqGuard<'a, T> {
    lock: &'a mut SpinlockRaw,
    _inner: &'a mut T,
    status: usize,
}

impl<T> Spinlock<T> {
    pub const fn new(inner: T) -> Self {
        Spinlock {
            lock: SpinlockRaw::new_unlocked(),
            _inner: UnsafeCell::new(inner),
        }
    }

    pub fn lock(&mut self) -> SpinGuard<'_, T> {
        self.lock.lock();
        SpinGuard {
            lock: &mut self.lock,
            _inner: unsafe { &mut *self._inner.get() },
        }
    }

    pub fn lock_irqsave(&mut self) -> SpinIrqGuard<'_, T> {
        let status = self.lock.lock_irqsave();
        SpinIrqGuard {
            lock: &mut self.lock,
            _inner: unsafe { &mut *self._inner.get() },
            status: status as usize,
        }
    }

    pub fn get_relaxed(&self) -> &T {
        unsafe { &*self._inner.get() }
    }
}

impl<'a, T> Deref for SpinGuard<'a, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        self._inner
    }
}

impl<'a, T> DerefMut for SpinGuard<'a, T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        self._inner
    }
}

impl<'a, T> Deref for SpinIrqGuard<'a, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        self._inner
    }
}

impl<'a, T> DerefMut for SpinIrqGuard<'a, T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        self._inner
    }
}

impl<'a, T> Drop for SpinGuard<'a, T> {
    fn drop(&mut self) {
        // Ensure the lock is released if still held
        self.lock.unlock();
    }
}

impl<'a, T> Drop for SpinIrqGuard<'a, T> {
    fn drop(&mut self) {
        // Ensure the lock is released if still held
        self.lock.unlock_irqrestore(self.status as c_int);
    }
}
