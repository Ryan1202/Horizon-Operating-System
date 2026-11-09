use core::{
    field::field_of,
    ops::{Index, IndexMut},
};

use crate::{
    cpu_local,
    kernel::{memory::percpu::PerCpuInit, time::Time},
    lib::rust::{
        bitset::BitSet,
        list::{ThinListHead, ThinListNode},
        spinlock::Spinlock,
    },
};

const LEVEL_BITS: usize = 6;
const LEVEL_SIZE: usize = 1 << LEVEL_BITS;

const LEVEL_COUNT: usize = 7;

const WHEEL_SIZE: usize = LEVEL_COUNT * LEVEL_SIZE;

type TimerOwner = field_of!(Timer, list);

cpu_local! {
    static CPU_TIMERS: TimerManager = TimerManager::default();
}

pub struct Timer {
    list: ThinListNode<Timer>,
}

struct TimerManagerInner {
    next_expiry: Time,

    pendings: BitSet<[usize; WHEEL_SIZE / usize::BITS as usize]>,
    wheels: [ThinListHead<TimerOwner>; WHEEL_SIZE],
}

const impl Default for TimerManagerInner {
    fn default() -> Self {
        Self {
            next_expiry: Time::new(),
            pendings: BitSet::zeroed(WHEEL_SIZE),
            wheels: [const { ThinListHead::default() }; WHEEL_SIZE],
        }
    }
}

const impl Index<usize> for TimerManagerInner {
    type Output = [ThinListHead<TimerOwner>; LEVEL_SIZE];

    fn index(&self, index: usize) -> &Self::Output {
        &self.wheels.as_chunks().0[index]
    }
}

const impl IndexMut<usize> for TimerManagerInner {
    fn index_mut(&mut self, index: usize) -> &mut Self::Output {
        &mut self.wheels.as_chunks_mut().0[index]
    }
}

struct TimerManager {
    inner: Spinlock<TimerManagerInner>,
}

unsafe impl PerCpuInit for TimerManager {}

const impl Default for TimerManager {
    fn default() -> Self {
        TimerManager {
            inner: Spinlock::new(TimerManagerInner::default()),
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn timers_init() {
    unsafe { CPU_TIMERS.init_possible_cpus() };
}
