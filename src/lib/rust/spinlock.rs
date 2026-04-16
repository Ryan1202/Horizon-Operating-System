//! 内核自旋锁的 Rust 实现并导出给 C 使用。
//!
//! 该模块提供与 `src/include/kernel/spinlock.h` 中内联函数对应的
//! C 可调用接口。实现直接操作底层的整数锁字（lock word），因此
//! 对于简单的 `volatile int` 版本和调试用的 `struct { int lock; ... }`
//! 版本均兼容（两者的首字段都是锁字）。

use core::cell::UnsafeCell;
use core::ffi::c_int;
use core::hint::spin_loop;
use core::marker::PhantomData;
use core::ops::{Deref, DerefMut};
use core::ptr::NonNull;
use core::sync::atomic::{AtomicU32, Ordering};

unsafe extern "C" {
    // 架构相关的辅助函数在其他地方提供（C/汇编）。
    fn save_eflags_cli() -> c_int;
    fn io_store_eflags(flags: c_int);
}

pub struct CSpinlock {
    ptr: NonNull<SpinlockRaw>,
}

impl CSpinlock {
    /// 从原始指针创建 `CSpinlock`，如果指针为 null 则返回 None。
    ///
    /// # Safety
    /// 调用者必须确保传入的指针有效且指向一个 `SpinlockRaw` 结构，否则行为未定义。
    pub unsafe fn from_ptr(ptr: *mut SpinlockRaw) -> Option<Self> {
        NonNull::new(ptr).map(|ptr| CSpinlock { ptr })
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
    lock: AtomicU32,
}

impl SpinlockRaw {
    /// 创建一个未加锁的自旋锁值（适用于静态初始化，例如 `Spinlock { lock: 0 }`）。
    pub const fn new_unlocked() -> Self {
        SpinlockRaw {
            lock: AtomicU32::new(0),
        }
    }

    /// 创建一个已加锁的自旋锁初始值。
    pub const fn new_locked() -> Self {
        SpinlockRaw {
            lock: AtomicU32::new(1),
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
            .compare_exchange(0, 1, Ordering::Acquire, Ordering::Relaxed)
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
            while self.lock.load(Ordering::Relaxed) != 0 {
                spin_loop();
            }
            if self
                .lock
                .compare_exchange_weak(0, 1, Ordering::Acquire, Ordering::Relaxed)
                .is_ok()
            {
                return;
            }
        }
    }

    #[inline]
    pub fn unlock(&self) {
        // 使用 Release 语义确保在释放锁之前的写对后续获取者可见
        self.lock.store(0, Ordering::Release);
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

/// RwSpinlock 是一个读写自旋锁，允许多个读者或一个写者访问受保护的数据。
///
/// 考虑到读写锁一般用于缓冲区，所以不将数据保存在内部
#[repr(C)]
pub struct RwSpinlock<T> {
    state: AtomicU32,
    _phantom: PhantomData<T>,
}

unsafe impl<T> Sync for RwSpinlock<T> {}

pub struct RwReadGuard<'a, T> {
    lock: &'a RwSpinlock<T>,
    ptr: NonNull<T>,
}

impl<'a, T> Into<RwWriteGuard<'a, T>> for RwReadGuard<'a, T> {
    fn into(self) -> RwWriteGuard<'a, T> {
        // 将读锁升级为写锁（需要先释放读锁再获取写锁）
        let lock = self.lock;
        let ptr = self.ptr;
        drop(self); // 释放读锁
        lock.write_lock(ptr)
    }
}

impl<'a, T> Into<RwReadGuard<'a, T>> for RwWriteGuard<'a, T> {
    fn into(self) -> RwReadGuard<'a, T> {
        // 将写锁降级为读锁（需要先释放写锁再获取读锁）
        let lock = self.lock;
        let ptr = self.ptr;
        drop(self);
        lock.read_lock(ptr)
    }
}

pub struct RwWriteGuard<'a, T> {
    lock: &'a RwSpinlock<T>,
    ptr: NonNull<T>,
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

impl<T> RwSpinlock<T> {
    const WRITER: u32 = 1 << 31;

    pub const fn new() -> Self {
        Self {
            state: AtomicU32::new(0),
            _phantom: PhantomData,
        }
    }

    pub fn read_lock(&self, ptr: NonNull<T>) -> RwReadGuard<'_, T> {
        loop {
            let state = self.state.load(Ordering::Relaxed);
            if state & Self::WRITER != 0 {
                spin_loop();
                continue;
            }

            if self
                .state
                .compare_exchange_weak(state, state + 1, Ordering::Acquire, Ordering::Relaxed)
                .is_ok()
            {
                return RwReadGuard { lock: self, ptr };
            }
        }
    }

    pub fn write_lock(&self, ptr: NonNull<T>) -> RwWriteGuard<'_, T> {
        loop {
            while self.state.load(Ordering::Relaxed) != 0 {
                spin_loop();
            }

            if self
                .state
                .compare_exchange_weak(0, Self::WRITER, Ordering::Acquire, Ordering::Relaxed)
                .is_ok()
            {
                return RwWriteGuard { lock: self, ptr };
            }
        }
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

impl<'a, T> Deref for RwReadGuard<'a, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        unsafe { self.ptr.as_ref() }
    }
}

impl<'a, T> Deref for RwWriteGuard<'a, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        unsafe { self.ptr.as_ref() }
    }
}

impl<'a, T> DerefMut for RwWriteGuard<'a, T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        unsafe { self.ptr.as_mut() }
    }
}

impl<'a, T> Drop for RwReadGuard<'a, T> {
    fn drop(&mut self) {
        self.lock.state.fetch_sub(1, Ordering::Release);
    }
}

impl<'a, T> Drop for RwWriteGuard<'a, T> {
    fn drop(&mut self) {
        self.lock.state.store(0, Ordering::Release);
    }
}
