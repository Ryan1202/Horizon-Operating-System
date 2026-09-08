//! descriptor 的状态与等待条件。
//!
//! management_lock 串行化管理操作；state 仅保护短时间状态访问。
//! leases 保护 mapping 所有权，references 保护 descriptor 存活；
//! readers 保护 action 节点，flows 保护包括 chip 回调在内的完整执行。

use super::{IrqNumber, IrqSharing, action::IrqAction};
use crate::{
    kernel::{
        interrupt::irq::{Flow, IrqData, IrqError},
        memory::kmalloc::Kmalloc,
    },
    lib::rust::spinlock::Spinlock,
};
use alloc::boxed::Box;
use core::{
    any::Any,
    mem::{self, MaybeUninit},
    ptr::NonNull,
    sync::atomic::AtomicUsize,
};

#[derive(PartialEq, Eq)]
pub(super) enum Status {
    Inactive,
    Active,
    Stopping,
}

pub(super) struct State {
    pub(super) status: Status,

    // /// drain 在锁内关闭此入口，随后不会再有新的 chip 回调。
    // pub(super) runtime_open: bool,
    /// 节点由 core 拥有。存在 readers 时，不得修改已发布节点的 next。
    pub(super) head: Option<NonNull<IrqAction>>,

    pub(super) sharing: IrqSharing,
    // /// 正在沿 action 链遍历的读者，保护节点及普通 next 链接。
    // pub(super) readers: usize,

    // /// 共享注销期间冻结新的遍历，已有读者退出后才能改 next。
    // pub(super) updating: bool,

    // /// 包含 ack/eoi/unmask 的完整 flow 数量，不能用 readers 替代。
    // pub(super) flows: usize,
    // pub(super) enabled_cpus: usize,
    // pub(super) affinity: Affinity,
    // pub(super) unhandled: u64,
}

impl State {
    pub(super) const fn new(sharing: IrqSharing) -> Self {
        Self {
            status: Status::Inactive,
            head: None,
            sharing,
        }
    }

    pub(super) fn add_action(
        &mut self,
        mut action: Box<IrqAction, Kmalloc>,
        sharing: IrqSharing,
    ) -> Result<NonNull<IrqAction>, IrqError> {
        // 冲突检查
        if !(self.sharing == IrqSharing::Shared && sharing == IrqSharing::Shared) {
            return Err(IrqError::Busy);
        }

        // 新节点只修改自己的 next，不修改已发布节点
        action.next = self.head;

        // SAFETY: Box 分配保证非 null
        let (action, _) = Box::into_non_null_with_allocator(action);

        self.head = Some(action);
        self.sharing = sharing;

        Ok(action)
    }

    pub(super) fn delete_action(&mut self, action: NonNull<IrqAction>) {
        let mut prev: Option<NonNull<IrqAction>> = None;
        let mut current = self.head;

        while let Some(curr) = current {
            if curr == action {
                // SAFETY: 由 Box 分配保证非 null
                let curr_ref = unsafe { curr.as_ref() };
                if let Some(mut prev) = prev {
                    // SAFETY: 由 Box 分配保证非 null
                    let prev_ref = unsafe { prev.as_mut() };
                    prev_ref.next = curr_ref.next;
                } else {
                    self.head = curr_ref.next;
                }
                return;
            }
            prev = current;
            // SAFETY: 由 Box 分配保证非 null
            current = unsafe { curr.as_ref().next };
        }
    }
}

pub struct IrqDescriptor {
    pub irq: IrqNumber,

    /// 仅在全局指针表锁下修改；零表示已经进入最终回收。
    pub leases: AtomicUsize,

    pub data: IrqData,
    pub flow: Flow,

    /// 管理路径可跨等待持有；IRQ 运行路径绝不取得此锁。
    // pub(super) management_lock: ManagementLock,

    ///// 只保护短期状态访问，不能跨 handler、chip 回调或等待。
    pub(super) state: Spinlock<State>,
}

impl IrqDescriptor {
    pub fn new(
        irq: IrqNumber,
        sharing: IrqSharing,
        domain: &'static dyn super::Domain,
        flow: Flow,
        arg: &dyn Any,
    ) -> Result<Self, IrqError> {
        let mut data = MaybeUninit::uninit();

        domain.allocate(irq, &mut data, arg)?;
        let data = unsafe { data.assume_init() };

        Ok(Self {
            irq,
            leases: AtomicUsize::new(0),
            data,
            flow,
            state: Spinlock::new(State::new(sharing)),
        })
    }

    pub fn realloc(
        &mut self,
        domain: &'static dyn super::Domain,
        arg: &dyn Any,
    ) -> Result<(), IrqError> {
        let state = self.state.lock();
        if state.status != Status::Inactive || state.head.is_some() || self.data.parent().is_some()
        {
            return Err(IrqError::Busy);
        }

        let mut data = MaybeUninit::uninit();

        domain.allocate(self.irq, &mut data, arg)?;
        let data = unsafe { data.assume_init() };

        let old = mem::replace(&mut self.data, data);
        old.domain().free(&old);

        Ok(())
    }
}
