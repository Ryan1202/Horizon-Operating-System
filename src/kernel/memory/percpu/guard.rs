use core::{ops::Deref, ptr::NonNull};

use crate::{
    arch::ArchCpuLocal,
    kernel::{
        memory::percpu::{CpuLocal, PerCpu, PerCpuDyn, PerCpuInit},
        thread::scheduler::PreemptGuard,
    },
};

pub struct CpuLocalGuard<'a, T: Sized> {
    preempt_guard: &'a PreemptGuard,
    inner: NonNull<T>,
}

impl<'a, T: Sized> CpuLocalGuard<'a, T> {
    pub fn new(preempt_guard: &'a PreemptGuard, percpu: &PerCpu<T>) -> Self
    where
        T: PerCpuInit,
    {
        let inner = NonNull::new(ArchCpuLocal::get_ptr(percpu) as *mut T).unwrap();
        Self {
            preempt_guard,
            inner,
        }
    }

    pub fn new_dyn(preempt_guard: &'a PreemptGuard, percpu: &'a PerCpuDyn<T>) -> Self
    where
        T: PerCpuInit,
    {
        let inner = NonNull::new(ArchCpuLocal::get_ptr_dyn(percpu) as *mut T).unwrap();
        Self {
            preempt_guard,
            inner,
        }
    }

    pub fn map<F, R>(self, f: F) -> CpuLocalGuard<'a, R>
    where
        F: FnOnce(&T) -> &R,
        R: PerCpuInit,
    {
        let inner = f(unsafe { self.inner.as_ref() });
        CpuLocalGuard {
            preempt_guard: self.preempt_guard,
            inner: NonNull::from(inner),
        }
    }

    pub fn preempt_guard(&self) -> &PreemptGuard {
        self.preempt_guard
    }
}

impl<'a, T: PerCpuInit> Deref for CpuLocalGuard<'a, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        unsafe { self.inner.as_ref() }
    }
}
