use core::{mem::replace, pin::Pin};

use crate::{
    kernel::{
        time::{Duration, SYSTEM_TIME, tick},
        topology::{CpuId, CpuMask, ThinCpuMask},
    },
    lib::rust::{
        field::field_path,
        list::{ListHead, ListNode},
        spinlock::{SpinInner, Spinlock},
    },
};

type EventListNode = field_path!(
    EventDevice => inner,
    SpinInner<EventDeviceInner>,
    EventDeviceInner => list
);

static EVENT_DEVICES: Spinlock<ListHead<EventListNode>> =
    unsafe { Spinlock::new_list_head(&raw const EVENT_DEVICES) };
static RELEASED_DEVICES: Spinlock<ListHead<EventListNode>> =
    unsafe { Spinlock::new_list_head(&raw const RELEASED_DEVICES) };

/// 时钟事件的统一接口，时钟事件设备不要求具有固定的频率，
/// 只需要能按照时间间隔在不同时刻发送中断事件即可
pub trait ClockEvent: Sync {
    fn shutdown(&self);

    fn set_one_shot(&self);
    fn set_periodic(&self);

    fn set_next_event(&self, duration: Duration);
}

/// 时钟事件的时间间隔限制
pub struct EventLimit {
    pub min: Duration,
    pub max: Duration,
}

impl EventLimit {
    pub fn new(min: Duration, max: Duration) -> Self {
        Self { min, max }
    }
}

#[derive(Clone, Copy)]
pub enum EventState {
    Detached,
    Shutdown,
    Periodic,
    OneShot,
}

/// 承载 `EventDevice` 中会在运行过程中被修改的字段，方便使用锁保护
struct EventDeviceInner {
    list: ListNode<EventDevice>,

    state: EventState,

    handler: fn(&'static EventDevice) -> Option<()>,
}

pub struct EventDevice {
    pub(super) priority: u8,
    limit: EventLimit,
    cpu_mask: ThinCpuMask,

    inner: Spinlock<EventDeviceInner>,

    private: &'static dyn ClockEvent,
}

impl EventDevice {
    pub const fn new(
        private: &'static dyn ClockEvent,
        limit: EventLimit,
        priority: u8,
        cpu_mask: CpuMask,
    ) -> Self {
        Self {
            inner: Spinlock::new(EventDeviceInner {
                list: ListNode::new(),
                state: EventState::Detached,
                handler: noop_handler,
            }),
            limit,
            priority,
            cpu_mask: ThinCpuMask::from(cpu_mask),
            private,
        }
    }

    pub fn register(mut self: Pin<&'static mut Self>) {
        unsafe {
            self.inner.init_with(|inner| {
                inner.state = EventState::Detached;
            });
        }

        let mut list = unsafe { Pin::new_unchecked(&EVENT_DEVICES) }.lock_pinned();

        unsafe {
            list.as_mut()
                .add_tail(&mut self.as_mut().get_unchecked_mut())
        };

        tick::try_new_device(self);
    }

    pub fn is_exclusive(&self, cpu: CpuId) -> bool {
        self.cpu_mask == ThinCpuMask::mask_of(cpu)
    }

    unsafe fn with_locked_inner<F, R>(self: Pin<&Self>, f: F) -> R
    where
        F: FnOnce(&mut EventDeviceInner) -> R,
    {
        unsafe {
            f(self
                .map_unchecked(|device| &device.inner)
                .lock_pinned()
                .as_mut()
                .get_unchecked_mut())
        }
    }

    pub fn get_state(self: Pin<&Self>) -> EventState {
        unsafe {
            self.map_unchecked(|device| &device.inner)
                .lock_pinned()
                .state
        }
    }

    pub fn take_handler(self: Pin<&Self>) -> fn(&'static EventDevice) -> Option<()> {
        unsafe { self.with_locked_inner(|inner| replace(&mut inner.handler, noop_handler)) }
    }

    pub fn detach(self: Pin<&Self>) {
        self.private.shutdown();
        unsafe {
            self.with_locked_inner(|inner| {
                inner.state = EventState::Detached;
            })
        };
    }

    pub fn shutdown(self: Pin<&Self>) {
        self.private.shutdown();
        unsafe {
            self.with_locked_inner(|inner| {
                inner.state = EventState::Shutdown;
            })
        };
    }

    pub fn set_periodic(self: Pin<&Self>) {
        self.private.set_periodic();
        unsafe {
            self.with_locked_inner(|inner| {
                inner.state = EventState::Periodic;
                inner.handler = periodic_handler;
            })
        };
    }

    pub fn set_one_shot(self: Pin<&Self>) {
        self.private.set_one_shot();
        unsafe {
            self.with_locked_inner(|inner| {
                inner.state = EventState::OneShot;
            })
        };
    }

    pub fn set_next_event(self: Pin<&Self>, next_event: Duration) -> Option<()> {
        if !matches!(self.get_state(), EventState::OneShot) {
            return None;
        }

        self.private.set_next_event(next_event);

        Some(())
    }

    /// 与另一时钟事件设备同步设置，用于方便替换
    pub fn sync_settings_with(self: Pin<&Self>, other: Pin<&Self>) {
        let mut self_inner = unsafe { self.map_unchecked(|this| &this.inner).lock_pinned() };
        let mut other_inner = unsafe { other.map_unchecked(|this| &this.inner).lock_pinned() };
        let inner = unsafe { self_inner.as_mut().get_unchecked_mut() };
        let other_inner = unsafe { other_inner.as_mut().get_unchecked_mut() };

        inner.handler = other_inner.handler;
        inner.state = other_inner.state;
        other_inner.handler = noop_handler;
    }

    /// 处理时钟事件
    pub fn handle_event(self: Pin<&'static Self>) -> Option<()> {
        let inner = unsafe { self.map_unchecked(|this| &this.inner) };

        (inner.get_relaxed().handler)(self.get_ref())
    }
}

fn noop_handler(_: &'static EventDevice) -> Option<()> {
    Some(())
}

fn periodic_handler(_: &'static EventDevice) -> Option<()> {
    SYSTEM_TIME.try_tick(1);

    Some(())
}
