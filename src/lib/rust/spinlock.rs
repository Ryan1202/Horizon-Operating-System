//! 内核自旋锁的 Rust 实现并导出给 C 使用。
//!
//! 该模块提供与 `src/include/kernel/spinlock.h` 中内联函数对应的
//! C 可调用接口。实现直接操作底层的整数锁字（lock word），因此
//! 对于简单的 `volatile int` 版本和调试用的 `struct { int lock; ... }`
//! 版本均兼容（两者的首字段都是锁字）。

use core::cell::{SyncUnsafeCell, UnsafeCell};
use core::ffi::c_int;
use core::hint::spin_loop;
use core::ops::{Deref, DerefMut};
use core::ptr::{NonNull, write};
use core::sync::atomic::{AtomicBool, Ordering};

unsafe extern "C" {
    // 架构相关的辅助函数在其他地方提供（C/汇编）。
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
    /// 创建一个未加锁的自旋锁值（适用于静态初始化，例如 `Spinlock { lock: 0 }`）。
    pub const fn new_unlocked() -> Self {
        SpinlockRaw {
            lock: AtomicBool::new(false),
        }
    }

    /// 创建一个已加锁的自旋锁初始值。
    pub const fn new_locked() -> Self {
        SpinlockRaw {
            lock: AtomicBool::new(true),
        }
    }

    pub fn init_with(&self, value: Self) {
        self.lock
            .store(value.lock.load(Ordering::Relaxed), Ordering::Relaxed);
    }

    #[inline]
    pub fn try_lock(&self) -> c_int {
        match self
            .lock
            .compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed)
        {
            Ok(_) => 1,
            Err(_) => 0,
        }
    }

    #[inline]
    pub fn lock(&self) {
        // TTAS（test-then-test-and-set）模式：先做短时间的 Relaxed 轮询，
        // 在看到为未加锁时再尝试通过弱 CAS 获取锁。弱 CAS 允许虚假失败，
        // 因此放在循环中是合理且高效的。
        loop {
            while self.lock.load(Ordering::Relaxed) == true {
                spin_loop();
            }
            if self
                .lock
                .compare_exchange_weak(false, true, Ordering::Acquire, Ordering::Relaxed)
                .is_ok()
            {
                return;
            }
        }
    }

    #[inline]
    pub fn unlock(&self) {
        // 使用 Release 语义确保在释放锁之前的写对后续获取者可见
        self.lock.store(false, Ordering::Release);
    }

    #[inline]
    pub fn lock_irqsave(&self) -> c_int {
        // 保存并关闭中断，然后获取锁，返回之前的中断状态
        let flags = unsafe { save_eflags_cli() };
        self.lock();
        flags
    }

    #[inline]
    pub fn try_lock_irqsave(&self) -> c_int {
        // 尝试在禁用中断的情况下获取锁；若失败则恢复中断并返回 0
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
    pub fn unlock_irqrestore(&self, flags: c_int) {
        // 释放锁并恢复之前的中断状态
        self.unlock();
        unsafe { io_store_eflags(flags) };
    }
}

#[repr(C)]
pub struct Spinlock<T> {
    pub(super) _inner: UnsafeCell<T>,
    lock: SpinlockRaw,
}

unsafe impl<T> Sync for Spinlock<T> {}

pub struct SpinGuard<'a, T> {
    lock: &'a SpinlockRaw,
    _inner: &'a mut T,
}

pub struct SpinIrqGuard<'a, T> {
    lock: &'a SpinlockRaw,
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

    /// 锁住自旋锁保护内部数据，通过`SpinGuard`提供安全的可变引用
    pub fn lock(&self) -> SpinGuard<'_, T> {
        self.lock.lock();
        SpinGuard {
            lock: &self.lock,
            _inner: unsafe { &mut *self._inner.get() },
        }
    }

    pub fn lock_irqsave(&self) -> SpinIrqGuard<'_, T> {
        // 禁用中断并获取锁，返回带有中断状态的`Guard`
        let status = self.lock.lock_irqsave();
        SpinIrqGuard {
            lock: &self.lock,
            _inner: unsafe { &mut *self._inner.get() },
            status: status as usize,
        }
    }

    pub fn get_relaxed(&self) -> &T {
        // 在无需同步语义（仅在明确知道安全的场景下）下获取对内部数据的只读访问
        unsafe { &*self._inner.get() }
    }

    /// 在受保护上下文中初始化内部数据。
    ///
    /// # Safety
    ///
    /// 调用者必须确保在非并发环境调用此方法
    pub unsafe fn init_with<F>(&self, f: F)
    where
        F: FnOnce(&mut T),
    {
        self.lock.init_with(SpinlockRaw::new_unlocked());
        let inner = unsafe { &mut *self._inner.get() };
        f(inner);
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
        // 在 Guard 被丢弃时释放锁（确保不会忘记释放）
        self.lock.unlock();
    }
}

impl<'a, T> Drop for SpinIrqGuard<'a, T> {
    fn drop(&mut self) {
        // 在 Guard 被丢弃时释放锁并恢复中断状态
        self.lock.unlock_irqrestore(self.status as c_int);
    }
}
