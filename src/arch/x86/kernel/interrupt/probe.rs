//! 一个在途的目标 LAPIC 查询
//!
//! 应答发布前必须完成同步 IPI 的 EOI

use core::sync::atomic::{AtomicU32, Ordering};

pub(crate) struct Probe(AtomicU32);

impl Probe {
    pub const fn new() -> Self {
        Self(AtomicU32::new(0))
    }

    pub fn start(&self, apic: u8, vector: u8) -> bool {
        let request = ((apic as u32) << 16) | ((vector as u32) << 8) | 1;
        self.0
            .compare_exchange(0, request, Ordering::AcqRel, Ordering::Relaxed)
            .is_ok()
    }

    pub fn requested(&self, apic: u8) -> Option<u8> {
        let request = self.0.load(Ordering::Acquire);
        (request & 0xff == 1 && request >> 16 == apic as u32).then_some((request >> 8) as u8)
    }

    pub fn complete(&self, busy: bool) {
        self.0.store(if busy { 3 } else { 2 }, Ordering::Release);
    }

    pub fn result(&self) -> Option<bool> {
        match self.0.load(Ordering::Acquire) {
            2 => Some(false),
            3 => Some(true),
            _ => None,
        }
    }

    pub fn release(&self) {
        self.0.store(0, Ordering::Release);
    }
}
