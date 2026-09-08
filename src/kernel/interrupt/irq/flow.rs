//! 封闭的 flow。物理事件 ack/eoi 一次，pending 的软件重放不伪造 EOI。
use super::{
    Flow,
    descriptor::{IrqDescriptor, Status},
};
use crate::kernel::thread::scheduler::PreemptGuard;
use core::sync::atomic::Ordering;

fn begin(descriptor: &IrqDescriptor, suppressed: bool) {
    let data = &descriptor.data;
    let chip = data.chip();
    match descriptor.flow {
        Flow::Edge => {
            if suppressed {
                chip.mask_ack(descriptor.irq, data);
            } else {
                chip.ack(descriptor.irq, data);
            }
        }
        Flow::Level => chip.mask_ack(descriptor.irq, data),
        Flow::FastEoi => {
            if suppressed {
                chip.mask(descriptor.irq, data);
            }
        }
        Flow::PerCpu => {
            if suppressed {
                chip.mask_ack(descriptor.irq, data);
            } else {
                chip.ack(descriptor.irq, data);
            }
        }
        Flow::Simple => {}
        _ => unimplemented!(),
    }
}

fn end(descriptor: &IrqDescriptor, physical: bool, resume: bool) {
    let data = &descriptor.data;
    let chip = data.chip();
    if physical && matches!(descriptor.flow, Flow::FastEoi | Flow::PerCpu) {
        chip.eoi(descriptor.irq, data);
    }
    if resume && matches!(descriptor.flow, Flow::Edge | Flow::Level | Flow::FastEoi) {
        chip.unmask(descriptor.irq, data);
    }
}

pub(super) fn replay(descriptor: &IrqDescriptor) {
    // dispatch(descriptor, false);
}

// pub(super) fn dispatch(descriptor: &IrqDescriptor, physical: bool) -> IrqResult {
//     // 即使管理路径触发软件重放，也禁止 handler 内阻塞和同步注销。
//     let _hardirq = crate::kernel::interrupt::HardIrqGuard::new();
//     let preempt = PreemptGuard::new();
//     let local = descriptor
//         .local
//         .as_ref()
//         .map(|local| local.get_local(&preempt));
//     let run = match &local {
//         Some(local) => &**local,
//         None => &descriptor.ordinary,
//     };
//     let (owner, suppressed) = {
//         let mut state = descriptor.state.lock_irqsave();
//         // Inactive 不得与 domain 的激活准备/释放并发调用 chip。
//         if !state.runtime_open {
//             return IrqResult::NotHandled;
//         }
//         if !physical && !run.pending.load(Ordering::Relaxed) {
//             return IrqResult::NotHandled;
//         }
//         state.flows += 1;
//         let suppressed = state.status != Status::Active
//             || state.updating
//             || run.depth.load(Ordering::Relaxed) != 0;
//         let owner = !suppressed && !run.running.load(Ordering::Relaxed);
//         if owner {
//             run.running.store(true, Ordering::Relaxed);
//             run.pending.store(false, Ordering::Relaxed);
//         } else {
//             run.pending.store(true, Ordering::Relaxed);
//         }
//         (owner, suppressed)
//     };
//     if physical {
//         begin(descriptor, suppressed);
//     }
//     if !owner {
//         end(descriptor, physical, false);
//         let mut state = descriptor.state.lock_irqsave();
//         state.flows -= 1;
//         descriptor.notify(&preempt);
//         return IrqResult::NotHandled;
//     }
//     let mut result = IrqResult::NotHandled;
//     let mut physical = physical;
//     loop {
//         let head = {
//             let mut state = descriptor.state.lock_irqsave();
//             if state.status != Status::Active
//                 || state.updating
//                 || run.depth.load(Ordering::Relaxed) != 0
//             {
//                 run.pending.store(true, Ordering::Relaxed);
//                 None
//             } else {
//                 state.readers += 1;
//                 Some(state.head)
//             }
//         };
//         if let Some(mut action) = head {
//             let mut handled = false;
//             while let Some(ptr) = action {
//                 // reader 保护整条普通 next 链；注册只头插，摘链需 drain readers。
//                 let current = unsafe { ptr.as_ref() };
//                 handled |= current.handler.handle(descriptor.irq) == IrqResult::Handled;
//                 action = current.next;
//             }
//             let mut state = descriptor.state.lock_irqsave();
//             state.readers -= 1;
//             if handled {
//                 result = IrqResult::Handled;
//             } else {
//                 state.unhandled = state.unhandled.saturating_add(1);
//             }
//             descriptor.notify(&preempt);
//         }
//         let resume = {
//             let state = descriptor.state.lock_irqsave();
//             // freeze 可能发生在 handler 退出与 unmask 之间；即使没有新事件，
//             // 更新路径也必须接管恢复投递，否则 Level 会永远保持屏蔽。
//             if state.status == Status::Active && state.updating {
//                 run.pending.store(true, Ordering::Relaxed);
//             }
//             state.status == Status::Active
//                 && !state.updating
//                 && run.depth.load(Ordering::Relaxed) == 0
//         };
//         // in_progress/flows 仍然持有，覆盖最后一次 eoi/unmask；stop 会再次 mask。
//         end(descriptor, physical, resume);
//         physical = false;
//         let mut state = descriptor.state.lock_irqsave();
//         if state.status == Status::Active
//             && !state.updating
//             && run.depth.load(Ordering::Relaxed) == 0
//             && run.pending.swap(false, Ordering::Relaxed)
//         {
//             drop(state);
//             continue;
//         }
//         run.running.store(false, Ordering::Relaxed);
//         state.flows -= 1;
//         descriptor.notify(&preempt);
//         return result;
//     }
// }
