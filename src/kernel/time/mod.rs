mod clockevent;
mod clocksource;
pub mod tick;
pub mod timer;

use core::{
    ops::{Add, AddAssign, Div, Mul},
    ptr::read_volatile,
};

pub use clockevent::{ClockEvent, EventDevice, EventLimit, EventState};

use crate::{
    kernel::{
        memory::percpu::{CPU_ID, PerCpuReadWrite},
        time::tick::{TICK_NS, TickState},
        topology::CpuId,
    },
    lib::rust::spinlock::Spinlock,
};

pub const NS_PER_SECOND: u32 = 1_000_000_000;
pub const TIMER_PERIOD: u32 = 1000;

pub static SYSTEM_TIME: SystemTime = SystemTime::new();

/// 系统维护的相对时间，全局唯一
///
/// 包括通过时钟事件统计的时钟滴答和通过时钟源计算的墙上时间
pub struct SystemTime {
    tick: Spinlock<TickState>,
    /// 负责更新的 CPU
    cpu: CpuId,
}

impl SystemTime {
    const fn new() -> Self {
        Self {
            tick: Spinlock::new(TickState::new()),
            cpu: CpuId::bsp(),
        }
    }

    /// 尝试触发滴答事件更新
    ////
    /// 只有在当前 CPU 是负责更新的 CPU 时才会更新
    fn try_tick(&self, ticks: usize) {
        let cpu = CPU_ID.read();

        if unsafe { read_volatile(&raw const self.cpu) } == cpu {
            self.tick.lock().tick(ticks);
        }
    }
}

/// 时间间隔，单位为纳秒
#[derive(Clone, Copy)]
pub struct Duration(u64);

impl Duration {
    pub const fn from_ns(ns: u64) -> Self {
        Duration(ns)
    }

    pub const fn from_us(us: u64) -> Self {
        Duration::from_ns(us.checked_mul(1000).expect("duration overflow"))
    }

    pub const fn from_ms(ms: u64) -> Self {
        Duration::from_us(ms.checked_mul(1000).expect("duration overflow"))
    }

    pub const fn from_s(s: u64) -> Self {
        Duration::from_ms(s.checked_mul(1000).expect("duration overflow"))
    }

    pub const fn from_min(min: u32) -> Self {
        Duration::from_s((min as u64).checked_mul(60).expect("duration overflow"))
    }

    pub const fn from_hours(h: u32) -> Self {
        Duration::from_min(h.checked_mul(60).expect("duration overflow"))
    }

    pub const fn from_days(d: u32) -> Self {
        Duration::from_hours(d.checked_mul(24).expect("duration overflow"))
    }

    pub const fn from_tick(ticks: usize) -> Self {
        Self::from_ns(
            (ticks as u64)
                .checked_mul(TICK_NS)
                .expect("duration overflow"),
        )
    }
}

const impl Add for Duration {
    type Output = Self;

    fn add(self, rhs: Self) -> Self::Output {
        Self(self.0.checked_add(rhs.0).expect("duration overflow"))
    }
}

const impl AddAssign for Duration {
    fn add_assign(&mut self, rhs: Self) {
        *self = *self + rhs;
    }
}

/// 相对固定基准的时间点，单位为纳秒
#[derive(Clone, Copy)]
pub struct Time(u64);

impl Time {
    pub const fn new() -> Self {
        Self(0)
    }
}

const impl Add<Duration> for Time {
    type Output = Self;

    fn add(self, rhs: Duration) -> Self::Output {
        Self(self.0.checked_add(rhs.0).expect("duration overflow"))
    }
}

const impl AddAssign<Duration> for Time {
    fn add_assign(&mut self, rhs: Duration) {
        *self = *self + rhs;
    }
}

#[derive(Clone, Copy)]
pub struct Factor {
    multiplier: u32,
    shift: u32,
}

impl Factor {
    /// 创建一个新的 Factor 实例
    ///
    /// `Factor` 的目标是将
    ///
    /// `(input * dst_freq) / src_freq` 转换为 `(input * multiplier) >> shift` 的形式
    ///
    /// 因此，multiplier 和 shift 的选择需要满足
    ///
    /// `multiplier * input >> shift` ≈ `input * dst_freq / src_freq`
    ///
    /// 所以  `multiplier` = `(dst_freq / src_freq) << shift`
    ///
    /// 为了提高整数运算的精度，调整为 `multiplier` = `(dst_freq << shift) / src_freq`
    ///
    /// 同时，为了保证 `input * multiplier` 不会溢出，
    ///
    /// 算出 `input` 的最大值，即 `max_seconds * src_freq` 的最大位数， 将剩余的位数给 `shift`
    pub const fn new(src_freq: u32, dst_freq: u32, max_seconds: u32) -> Self {
        let mut shiftacc = 32;

        let mut tmp = (max_seconds as u64 * src_freq as u64) >> 32;
        while tmp != 0 {
            tmp >>= 1;
            shiftacc -= 1;
        }

        let mut shift = 32;
        while shift > 0 {
            tmp = (dst_freq as u64) << shift;
            tmp = rounded_div::u64(tmp, src_freq as u64);

            // 检查 shift 位数是否足够
            if tmp >> shiftacc == 0 {
                break;
            }

            shift -= 1;
        }

        Factor {
            multiplier: tmp as u32,
            shift,
        }
    }

    pub const fn to_duration_floor(&self, input: u64) -> Duration {
        debug_assert!(input <= (u64::MAX >> self.shift));
        let value = (input << self.shift) / self.multiplier as u64;
        Duration(value)
    }

    pub const fn to_duration_ceil(&self, input: u64) -> Duration {
        debug_assert!(input <= (u64::MAX >> self.shift));
        let value = (input << self.shift).div_ceil(self.multiplier as u64);
        Duration(value)
    }
}

const impl Div<Factor> for Duration {
    type Output = u64;

    fn div(self, rhs: Factor) -> Self::Output {
        (self.0 * rhs.multiplier as u64) >> rhs.shift
    }
}

const impl Mul<u64> for Factor {
    type Output = Duration;

    fn mul(self, rhs: u64) -> Self::Output {
        self.to_duration_floor(rhs)
    }
}
