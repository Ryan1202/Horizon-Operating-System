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
    dispatch_event(descriptor, true)
}

pub(super) fn replay(descriptor: &IrqDescriptor) {
    dispatch_event(descriptor, false);
}

fn dispatch_event(descriptor: &IrqDescriptor, mut physical: bool) -> Option<()> {
    let _preempt = PreemptGuard::new();
    let _hardirq = HardIrqGuard::new();

    let action = {
        let mut state = descriptor.state.lock_irqsave();
        if state.status == Status::Inactive {
            return None;
        }

        if state.status != Status::Active {
            if !physical {
                return None;
            }
            if state.status == Status::Stopping {
                state.pending = true;
            }

            let data = &descriptor.data;
            match descriptor.flow {
                Flow::Edge | Flow::Level => data.chip().mask_ack(data),
                Flow::FastEoi => {
                    data.chip().mask(data);
                    data.chip().eoi(data);
                }
                Flow::Simple => {}
                _ => unreachable!("unsupported IRQ flow"),
            }

            return None;
        }

        let action = state.head.expect("active IRQ without action");

        if !physical && !state.pending {
            return None;
        }

        let running = descriptor.in_progress.load(Ordering::Relaxed);

        if physical {
            begin(descriptor, running);
        }

        if running {
            state.pending = true;

            if physical && descriptor.flow == Flow::FastEoi {
                descriptor.data.chip().eoi(&descriptor.data);
            }

            return None;
        }

        descriptor.in_progress.store(true, Ordering::Relaxed);
        state.pending = false;
        action
    };

    let mut result = None;
    loop {
        // 注销先关闭入口再排空，因此节点在本次执行期间不会摘除或释放
        // SAFETY: in_progress 在整个 handler 和 chip 收尾期间保持为 true；
        let mut current = Some(action);
        while let Some(pointer) = current {
            // SAFETY: 管理修改等待整个 in_progress，next 和 enabled 在遍历期间不变。
            let node = unsafe { pointer.as_ref() };
            if node.enabled && node.handler.handle(descriptor.irq).is_some() {
                result = Some(());
            }
            current = node.next;
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
