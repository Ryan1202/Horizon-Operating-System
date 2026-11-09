use core::{cell::SyncUnsafeCell, pin::Pin};

use alloc::sync::Arc;

use crate::{
    arch::x86::kernel::interrupt::legacy::request_isa_irq,
    kernel::{
        interrupt::irq::{IrqHandle, IrqHandler, IrqNumber, IrqSharing},
        memory::kmalloc::Kmalloc,
        time::EventDevice,
    },
};

mod pit;

const LEGACY_TIMER_IRQ_RAW: u8 = 0;

static TIMER_HANDLE: SyncUnsafeCell<Option<IrqHandle>> = SyncUnsafeCell::new(None);

static GLOBAL_CLOCK_EVENT: SyncUnsafeCell<Option<Pin<&'static EventDevice>>> =
    SyncUnsafeCell::new(None);

pub struct TimerHandler;

impl IrqHandler for TimerHandler {
    fn handle(&self, _: IrqNumber) -> Option<()> {
        if let Some(clock_event) = unsafe { *GLOBAL_CLOCK_EVENT.get() } {
            clock_event.handle_event()
        } else {
            Some(())
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn x86_timer_init() {
    pit::pit_init();

    let handler = Arc::new_in(TimerHandler, Kmalloc::default());
    unsafe {
        let mut handle = request_isa_irq(LEGACY_TIMER_IRQ_RAW, IrqSharing::Exclusive, handler)
            .expect("Failed to request IRQ0!");

        handle.enable_irq();

        (*TIMER_HANDLE.get()) = Some(handle);
    }
}
