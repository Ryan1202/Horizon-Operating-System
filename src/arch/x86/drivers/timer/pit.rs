use core::{cell::SyncUnsafeCell, pin::Pin};

use crate::{
    arch::x86::{asm::io_out8, drivers::timer::GLOBAL_CLOCK_EVENT},
    kernel::{
        time::{
            ClockEvent, Duration, EventDevice, EventLimit, Factor, NS_PER_SECOND, TIMER_PERIOD,
        },
        topology::{CpuId, CpuMask},
    },
    lib::rust::spinlock::Spinlock,
};

const PIT_CONROL: u16 = 0x43;
const PIT_COUNTER0: u16 = 0x40;

const FREQUENCY: u32 = 1193182;
const LATCH: u16 = rounded_div::u32(FREQUENCY, TIMER_PERIOD) as u16;

const FACTOR: Factor = Factor::new(NS_PER_SECOND, FREQUENCY, 1);

const LIMIT: EventLimit = EventLimit {
    min: Duration::from_us(10),
    max: Duration::from_ms(10),
};

static PIT_PRIVATE: Pit = Pit {
    factor: FACTOR,
    inner: Spinlock::new(PitInner {}),
};

pub static PIT: SyncUnsafeCell<EventDevice> = SyncUnsafeCell::new(EventDevice::new(
    &PIT_PRIVATE,
    LIMIT,
    0,
    CpuMask::mask_of(CpuId::bsp()),
));

struct PitInner {}

impl PitInner {
    fn set_control(&mut self, control: u8) {
        unsafe {
            io_out8(PIT_CONROL, control);
        }
    }

    fn set_counter0(&mut self, count: u16) {
        let count = count.to_le_bytes();

        unsafe {
            io_out8(PIT_COUNTER0, count[0]);
            io_out8(PIT_COUNTER0, count[1]);
        }
    }
}

pub struct Pit {
    factor: Factor,
    inner: Spinlock<PitInner>,
}

impl ClockEvent for Pit {
    fn set_one_shot(&self) {
        self.inner.lock().set_control(0x38);
    }

    fn set_next_event(&self, duration: Duration) {
        self.inner
            .lock()
            .set_counter0((duration / self.factor) as u16);
    }

    fn set_periodic(&self) {
        let mut inner = self.inner.lock();

        inner.set_control(0x34);
        inner.set_counter0(LATCH);
    }

    fn shutdown(&self) {
        let mut inner = self.inner.lock();

        // 真实硬件写入 0x30 就会停止计数了
        inner.set_control(0x30);

        // 但在一些虚拟硬件上还会继续计数，所以再写入 0 ( 在 PIT 看来是 65536 )
        inner.set_counter0(0);

        // 再写入 0x30，计数器停止
        inner.set_control(0x30);
    }
}

pub fn pit_init() {
    unsafe {
        *GLOBAL_CLOCK_EVENT.get() = Some(Pin::new_unchecked(&*PIT.get()));

        let pit = Pin::new_unchecked(&mut *PIT.get());
        pit.register();
    }
}
