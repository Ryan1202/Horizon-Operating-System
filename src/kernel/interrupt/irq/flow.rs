use super::descriptor::{IrqDescriptor, Status};
use core::sync::atomic::Ordering;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Flow {
    Edge,
    Level,
    FastEoi,
    EdgeEoi,
    Simple,
    Bad,
}

fn begin(descriptor: &IrqDescriptor) {
    let data = &descriptor.data;
    let chip = data.chip();

    match descriptor.flow {
        Flow::Edge | Flow::EdgeEoi => chip.ack(data),
        Flow::Level => chip.mask_ack(data),
        Flow::FastEoi | Flow::Simple => {}
        _ => unreachable!("unsupported IRQ flow"),
    }
}

fn end(descriptor: &IrqDescriptor, resume: bool) {
    let data = &descriptor.data;
    let chip = data.chip();

    match descriptor.flow {
        Flow::Edge => {}
        Flow::Level if resume => chip.unmask(data),
        Flow::Level => {}
        Flow::FastEoi | Flow::EdgeEoi => chip.eoi(data),
        Flow::Simple => {}
        _ => unreachable!("unsupported IRQ flow"),
    }
}

fn skip(descriptor: &IrqDescriptor) {
    let data = &descriptor.data;
    let chip = data.chip();

    match descriptor.flow {
        Flow::Edge => chip.ack(data),
        Flow::Level => chip.mask_ack(data),
        Flow::FastEoi => chip.eoi(data),
        Flow::EdgeEoi => {
            chip.ack(data);
            chip.eoi(data);
        }
        Flow::Simple => {}
        _ => unreachable!("unsupported IRQ flow"),
    }
}

pub(super) fn dispatch(descriptor: &IrqDescriptor) -> Option<()> {
    let action = {
        let state = descriptor.state.lock_irqsave();
        if state.status != Status::Enabled {
            drop(state);
            skip(descriptor);
            return None;
        }

        let action = unsafe { descriptor.actions.load(Ordering::Acquire).as_ref() }
            .expect("active IRQ without action");

        begin(descriptor);

        let running = descriptor.in_progress.fetch_add(1, Ordering::Relaxed);
        if running > 0 {
            return None;
        }

        action
    };

    let mut result = None;
    loop {
        let mut current = Some(action);
        while let Some(action) = current {
            if action.enabled.load(Ordering::Relaxed)
                && action.handler.handle(descriptor.irq).is_some()
            {
                result = Some(());
            }
            current = action.next.map(|ptr| unsafe { ptr.as_ref() });
        }
        {
            let state = descriptor.state.lock();
            end(descriptor, matches!(state.status, Status::Enabled));
        }

        let running = descriptor.in_progress.fetch_sub(1, Ordering::Relaxed);
        if running > 1 {
            continue;
        }

        return result;
    }
}
