use core::{cell::SyncUnsafeCell, hint::spin_loop, mem::MaybeUninit, ops::Range, pin::Pin};

use crate::{
    arch::{
        ArchInterrupt,
        x86::{
            drivers::apic::{
                LocalApic,
                lapic::{LAPIC, Regs, reg::CommonReg},
            },
            kernel::interrupt::vector::APIC_TIMER_VECTOR,
        },
    },
    cpu_local,
    kernel::{
        interrupt::Interrupt,
        memory::percpu::PerCpuInit,
        time::{ClockEvent, EventDevice, SystemTime, TIMER_PERIOD},
        topology::{CpuId, CpuMask},
    },
    printk,
};

const CALIBRATE_LOOPS: u64 = TIMER_PERIOD as u64 / 10;
const DIVIDE_CONFIG: DivideConfig = DivideConfig::By16;
static LAPIC_PERIOD: SyncUnsafeCell<u32> = SyncUnsafeCell::new(0);
static LAPIC_TIMER: SyncUnsafeCell<Option<LapicTimer>> = SyncUnsafeCell::new(None);

// 中断路径验证用
static VALIDATE_LOOP: SyncUnsafeCell<Option<u64>> = SyncUnsafeCell::new(None);
static VALIDATE_COUNTER: SyncUnsafeCell<Range<Counter>> =
    SyncUnsafeCell::new(Counter::new()..Counter::new());

const LVT_MASK_BIT: u32 = 16;

cpu_local! {
    static EVENT_DEVICE: MaybeUninit<LapicTimerDevice> = MaybeUninit::uninit();
}

struct LapicTimerDevice {
    pub inner: EventDevice,
}

unsafe impl PerCpuInit for LapicTimerDevice {
    fn init(this: &mut MaybeUninit<Self>, cpu: CpuId) {
        let period = unsafe { *LAPIC_PERIOD.get() };
        let frequency = u64::from(period) * u64::from(TIMER_PERIOD) / DIVIDE_CONFIG.factor();

        assert_ne!(period, 0, "Local APIC Timer is not calibrated!");

        this.write(LapicTimerDevice {
            inner: EventDevice::new(
                unsafe {
                    (*LAPIC_TIMER.get())
                        .as_ref()
                        .expect("Init LapicTimerDevice before LapicTimer!")
                },
                0xf..0x7fffffff,
                frequency as u32,
                1,
                CpuMask::mask_of(cpu),
            ),
        });
    }
}

#[derive(Clone, Copy)]
#[repr(u32)]
enum DivideConfig {
    By1 = 0b1011,
    By2 = 0b0000,
    By4 = 0b0001,
    By8 = 0b0010,
    By16 = 0b0011,
    By32 = 0b1000,
    By64 = 0b1001,
    By128 = 0b1010,
}

const impl From<u32> for DivideConfig {
    fn from(value: u32) -> Self {
        match value & 0x0f {
            0b1011 => Self::By1,
            0b0000 => Self::By2,
            0b0001 => Self::By4,
            0b0010 => Self::By8,
            0b0011 => Self::By16,
            0b1000 => Self::By32,
            0b1001 => Self::By64,
            0b1010 => Self::By128,
            _ => panic!("illegal DeviceConfig value"),
        }
    }
}

impl DivideConfig {
    const fn factor(&self) -> u64 {
        match self {
            Self::By1 => 1,
            Self::By2 => 2,
            Self::By4 => 4,
            Self::By8 => 8,
            Self::By16 => 16,
            Self::By32 => 32,
            Self::By64 => 64,
            Self::By128 => 128,
        }
    }
}

#[derive(Clone, Copy)]
#[repr(u32)]
pub enum TimerMode {
    OneShot = 0x000000,
    Periodic = 0x020000,
    TscDeadline = 0x040000,
}

const impl From<u32> for TimerMode {
    fn from(value: u32) -> Self {
        match value & 0x060000 {
            0x000000 => Self::OneShot,
            0x020000 => Self::Periodic,
            0x040000 => Self::TscDeadline,
            _ => panic!("illegal TimerMode value"),
        }
    }
}

struct Counter {
    /// LAPIC Timer 的计数
    counter: u64,
    /// jiffies 计数
    jiffies: u64,
}

impl Counter {
    const fn new() -> Self {
        Self {
            counter: 0,
            jiffies: 0,
        }
    }

    fn read(regs: &dyn Regs) -> Self {
        let counter = regs.read_common(CommonReg::Ccr) as u64;
        let jiffies = SystemTime::jiffies();
        Self { counter, jiffies }
    }
}

pub struct LapicTimer {
    regs: &'static dyn Regs,
}

unsafe impl PerCpuInit for LapicTimer {}
unsafe impl Sync for LapicTimer {}

impl LapicTimer {
    const fn new(regs: &'static dyn Regs) -> Self {
        Self { regs }
    }

    fn calibrate(&self) {
        self.set_lvt(0xffffffff, TimerMode::Periodic, true);

        let mut jiffies_last = SystemTime::jiffies();
        let mut start = Counter::new();
        let mut end = Counter::new();
        let regs = self.regs;

        let status = ArchInterrupt::get_status();
        ArchInterrupt::enable();

        for i in 0..=CALIBRATE_LOOPS {
            // 自旋等待 jiffies 增加（触发 clockevent），从而在时钟事件的边界计数提高准确度
            loop {
                let jiffies_now = SystemTime::jiffies();

                if jiffies_now > jiffies_last {
                    jiffies_last = jiffies_now;
                    break;
                }

                spin_loop();
            }

            ArchInterrupt::disable();
            if i == 0 {
                start = Counter::read(regs);
            } else {
                end = Counter::read(regs);
            }
            ArchInterrupt::enable();
        }

        ArchInterrupt::disable();

        let delta = start.counter - end.counter;
        let period = delta * DIVIDE_CONFIG.factor() / CALIBRATE_LOOPS;
        unsafe {
            *LAPIC_PERIOD.get() = period as u32;
        }

        let freq = period / (1000000 / TIMER_PERIOD as u64);
        let freq_remain = period % (1000000 / TIMER_PERIOD as u64);
        printk!(
            "LAPIC Timer Calibrate: Delta: {}, Bus Clock: {}.{:04} MHz\n",
            delta,
            freq,
            freq_remain
        );

        ArchInterrupt::enable();
        self.set_lvt(period as u32, TimerMode::Periodic, false);

        let n_loop = unsafe { &*VALIDATE_LOOP.get() };
        while n_loop.is_none_or(|n| n < CALIBRATE_LOOPS) {
            spin_loop();
        }
        ArchInterrupt::disable();

        let counters = unsafe { &*VALIDATE_COUNTER.get() };
        let jiffies_delta = counters.end.jiffies - counters.start.jiffies;
        printk!("LAPIC Timer Validate: Jiffies: {}\n", jiffies_delta);

        unsafe { EVENT_DEVICE.init_current_cpu() };

        ArchInterrupt::restore(&status);
    }

    fn set_lvt(&self, clock: u32, mode: TimerMode, mask: bool) {
        let regs = self.regs;

        let value = (mode as u32) | ((mask as u32) << LVT_MASK_BIT) | APIC_TIMER_VECTOR as u32;
        regs.write_common(CommonReg::Timer, value);

        let value = (clock as u64 / DIVIDE_CONFIG.factor()) as u32;
        regs.write_common(CommonReg::Dcr, DIVIDE_CONFIG as u32);
        regs.write_common(CommonReg::Icr, value);
    }
}

impl ClockEvent for LapicTimer {
    fn set_periodic(&self) {
        let clock = unsafe { *LAPIC_PERIOD.get() };
        self.set_lvt(clock, TimerMode::Periodic, false);
    }

    fn set_one_shot(&self) {
        self.set_lvt(0, TimerMode::OneShot, false);
    }

    fn set_next_event(&self, ticks: u64) {
        self.regs.write_common(CommonReg::Icr, ticks as u32);
    }

    fn shutdown(&self) {
        let mut value = self.regs.read_common(CommonReg::Timer);
        value |= 1 << LVT_MASK_BIT;
        self.regs.write_common(CommonReg::Timer, value);
        self.regs.write_common(CommonReg::Icr, 0);
    }
}

impl LocalApic {
    pub(super) fn handle_timer(&self) {
        let device = EVENT_DEVICE.get_remote(CpuId::current()).unwrap();
        unsafe {
            let event_device = &(*device).assume_init_ref().inner;

            Pin::new_unchecked(event_device).handle_event();
        }
    }

    pub(super) fn validate_timer(&self) {
        let n_loop = unsafe { &mut *VALIDATE_LOOP.get() };
        let counter = unsafe { &mut *VALIDATE_COUNTER.get() };
        let regs = self.reg();

        if let Some(n_loop) = n_loop {
            if *n_loop >= CALIBRATE_LOOPS {
                return;
            }
            *n_loop += 1;

            counter.end = Counter::read(regs);
        } else {
            counter.start = Counter::read(regs);
            *n_loop = Some(0);
        }
    }
}

pub(super) fn timer_init(regs: &'static dyn Regs) {
    unsafe {
        *LAPIC_TIMER.get() = Some(LapicTimer::new(regs));
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn lapic_setup_bsp_timer() {
    let timer =
        unsafe { (*LAPIC_TIMER.get()).as_ref() }.expect("Local APIC Timer is not initialized");

    timer.calibrate();

    unsafe {
        let ptr = EVENT_DEVICE
            .get_remote(CpuId::bsp())
            .expect("Local APIC Timer is not initialized")
            as *mut MaybeUninit<LapicTimerDevice>;

        let ptr = &mut (*ptr).assume_init_mut().inner;
        let this = Pin::new_unchecked(ptr);

        let lapic = (*LAPIC.get()).assume_init_mut();
        lapic.handle_timer = LocalApic::handle_timer;

        this.register();
    }
}
