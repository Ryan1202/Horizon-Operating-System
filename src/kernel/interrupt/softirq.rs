use core::sync::atomic::{AtomicU8, Ordering};

const COUNT: u8 = 5;

static PENDING: AtomicU8 = AtomicU8::new(0);

unsafe extern "C" {
    fn softirq_dispatch(pending: u8);
}

#[unsafe(export_name = "softirq_raise")]
extern "C" fn raise(kind: u8) {
    debug_assert!((0..COUNT).contains(&kind), "invalid softirq type");
    assert!(
        !super::in_softirq(),
        "a softirq handler must not raise softirq directly"
    );
    PENDING.fetch_or(1 << kind, Ordering::Release);
}

pub(super) fn drain() {
    loop {
        let pending = PENDING.swap(0, Ordering::Relaxed);
        if pending == 0 {
            return;
        }

        // SAFETY: pending 只包含 SoftirqType 范围内的位；C handler 表在
        // softirq 执行前完成注册，之后只读。
        unsafe { softirq_dispatch(pending) };
    }
}

pub(super) fn pending() -> bool {
    PENDING.load(Ordering::Relaxed) != 0
}
