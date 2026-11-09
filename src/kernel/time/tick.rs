use core::{
    cell::Cell,
    pin::Pin,
    sync::atomic::{AtomicBool, Ordering},
};

use crate::{
    arch::ArchInterrupt,
    cpu_local,
    kernel::{
        interrupt::Interrupt,
        memory::percpu::{CPU_ID, PerCpuInit, PerCpuReadWrite},
        thread::PreemptGuard,
        time::{Duration, EventDevice, NS_PER_SECOND, TIMER_PERIOD, Time},
    },
};

pub(super) const TICK_NS: u64 = rounded_div::u64(NS_PER_SECOND as u64, TIMER_PERIOD as u64);

/// 全局的时钟滴答状态
pub struct TickState {
    /// 当前的 jiffies 值，单位为时钟滴答，通过时钟事件触发增长，一个 `TIME_PERIOD` 对应一次计数
    ///
    /// 除了回绕外单调递增，但不保证与实际时间同步
    jiffies: u64,
    /// 下一次周期性事件的时间，单位为纳秒
    ///
    /// 可以看作是虚拟时间，通过时钟滴答设备的中断测量得到，不与实际时间挂钩
    next_period_time: Time,
}

impl TickState {
    pub(super) const fn new() -> Self {
        Self {
            jiffies: 0,
            next_period_time: Time::new(),
        }
    }

    /// 触发滴答事件更新
    pub(super) const fn tick(&mut self, ticks: usize) {
        self.jiffies += ticks as u64;
        self.next_period_time += Duration::from_tick(ticks);
    }
}

/// 时钟滴答设备
///
/// `TickDevice` 是每个 CPU 一个的描述结构，为每个 CPU 绑定一个时钟事件设备
pub struct TickDevice {
    /// 事件设备
    ///
    /// `None` 表示当前 CPU 没有绑定事件设备
    ///
    /// `Some` 表示当前 CPU 通过不可变引用绑定了一个事件设备，一个事件设备可能被多个 CPU 绑定
    event_device: Cell<Option<Pin<&'static EventDevice>>>,
    /// 事件类型
    ///
    /// `true` 表示单次事件，`false` 表示周期性事件
    is_one_shot: AtomicBool,
}

unsafe impl PerCpuInit for TickDevice {}

cpu_local! {
    static TICK_DEVICES: TickDevice = TickDevice {
        event_device: Cell::new(None),
        is_one_shot: AtomicBool::new(false),
    };
}

/// 检查当前 CPU 的时钟事件设备是否应该被替换为新的事件设备
fn should_replace(current_device: Option<&EventDevice>, new_device: &EventDevice) -> bool {
    let cpu_id = CPU_ID.read();

    let new_exclusive = new_device.is_exclusive(cpu_id);
    let cur_exclusive = current_device.is_some_and(|cur| cur.is_exclusive(cpu_id));

    // 有 3 种情况
    // 1. 新旧设备都是或都不是独占的，需要比较优先级
    // 2. 新旧设备只有一个是独占的，只有新设备是独占的时允许绑定
    // 3. 没有旧事件设备，允许绑定

    if let Some(cur) = current_device {
        if new_exclusive == cur_exclusive {
            // 新旧设备都是或都不是独占的，需要比较优先级
            new_device.priority > cur.priority
        } else {
            new_exclusive
        }
    } else {
        true
    }
}

fn replace_device(tick_device: &TickDevice, new: Pin<&'static EventDevice>) {
    let old = tick_device.event_device.replace(Some(new));

    if let Some(old) = old {
        old.detach();
    }

    new.shutdown();

    if let Some(old) = old {
        new.sync_settings_with(old);
    } else {
        // 默认使用周期模式
        tick_device.is_one_shot.store(false, Ordering::Relaxed);
    }

    if tick_device.is_one_shot.load(Ordering::Relaxed) {
        new.set_one_shot();
    } else {
        new.set_periodic();
    }
}

pub fn try_new_device(event_device: Pin<&'static mut EventDevice>) {
    let _interrupt = ArchInterrupt::save_and_disable();
    let guard = PreemptGuard::new();
    let tick_device = TICK_DEVICES.get_local(&guard);
    let current_device = tick_device.event_device.get();

    let new_device = event_device.into_ref();
    if should_replace(current_device.map(Pin::get_ref), new_device.get_ref()) {
        replace_device(&tick_device, new_device);
    }
}
