//! 状态锁串行化 chip 操作；handler 在锁外执行，pending 重放不伪造 ack/EOI

use super::{
    Flow,
    descriptor::{IrqDescriptor, Status},
};
use crate::kernel::{interrupt::HardIrqGuard, thread::scheduler::PreemptGuard};
use core::sync::atomic::Ordering;

/// 调用方持有 descriptor 状态锁；每次实际进入只调用一次
fn begin(descriptor: &IrqDescriptor, running: bool) {
    let data = &descriptor.data;
    let chip = data.chip();

    match descriptor.flow {
        Flow::Edge => chip.ack(data),
        Flow::Level => chip.mask_ack(data),
        Flow::FastEoi => {
            if running {
                chip.mask(data);
            }
        }
        Flow::Simple => {}
        _ => unreachable!("unsupported IRQ flow"),
    }
}

pub(super) fn dispatch(descriptor: &IrqDescriptor) -> Option<()> {
    let _preempt = PreemptGuard::new();
    let _hardirq = HardIrqGuard::new();

    let action = {
        let mut state = descriptor.state.lock_irqsave();
        if state.status != Status::Active {
            return None;
        }

        let action = state.head.expect("active IRQ without action");
        let running = descriptor.in_progress.load(Ordering::Relaxed);
        begin(descriptor, running);

        if running {
            state.pending = true;

            if descriptor.flow == Flow::FastEoi {
                descriptor.data.chip().eoi(&descriptor.data);
            }

            return None;
        }

        descriptor.in_progress.store(true, Ordering::Relaxed);
        state.pending = false;
        action
    };

    let mut result = None;
    let mut physical = true;

    loop {
        // 注销先关闭入口再排空，因此节点在本次执行期间不会摘除或释放
        // SAFETY: in_progress 在整个 handler 和 chip 收尾期间保持为 true；
        if unsafe { action.as_ref() }
            .handler
            .handle(descriptor.irq)
            .is_some()
        {
            result = Some(());
        }

        let mut state = descriptor.state.lock_irqsave();
        if physical && descriptor.flow == Flow::FastEoi {
            descriptor.data.chip().eoi(&descriptor.data);
        }
        physical = false;

        if state.status == Status::Active && state.pending {
            state.pending = false;
            drop(state);
            continue;
        }

        if state.status == Status::Active && matches!(descriptor.flow, Flow::Level | Flow::FastEoi)
        {
            descriptor.data.chip().unmask(&descriptor.data);
        }

        descriptor.in_progress.store(false, Ordering::Relaxed);
        return result;
    }
}
