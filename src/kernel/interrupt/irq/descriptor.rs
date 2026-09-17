use alloc::boxed::Box;

use super::{
    Flow, IrqData, IrqError, IrqNumber, IrqSharing, action::IrqAction, placeholder::Placeholder,
};
use crate::{
    kernel::{interrupt::irq::IrqReservation, memory::kmalloc::Kmalloc},
    lib::rust::spinlock::Spinlock,
};
use core::{
    any::Any,
    hint::spin_loop,
    mem,
    ptr::NonNull,
    sync::atomic::{AtomicPtr, AtomicUsize, Ordering},
};

#[derive(Clone, Copy, PartialEq, Eq)]
pub(super) enum Status {
    /// 尚未激活
    Inactive,
    /// 已激活，但未启用任何 handler
    Disabled,
    /// 已激活，至少有一个 handler 已启用
    Enabled,
    /// 正在停用，禁止新的 handler 执行者进入，正在等待所有旧执行者退出
    Stopping,
}

pub(super) struct State {
    /// 当前的状态
    pub(super) status: Status,
    /// 已注册且可用的 handler 数量
    pub(super) active: usize,
}

impl State {
    const fn new() -> Self {
        Self {
            status: Status::Inactive,
            active: 0,
        }
    }
}

pub struct IrqDescriptor {
    /// 全局 IRQ 编号
    pub irq: IrqNumber,
    /// IRQ 的数据，每个中断 chip 维护一份自己的数据
    pub data: IrqData,
    /// IRQ 的处理流程
    pub flow: Flow,
    /// 已注册的 handler 链表头指针
    pub(super) actions: AtomicPtr<IrqAction>,
    /// 当前的状态
    pub(super) state: Spinlock<State>,
    /// 共享/独占
    pub(super) sharing: IrqSharing,
    /// 当前正在执行的 handler 数量
    pub(super) in_progress: AtomicUsize,
}

impl IrqDescriptor {
    /// 调用方已把状态切到 Stopping，因此不会再有新的 handler 执行者进入
    pub(super) fn drain(&self) {
        while self.in_progress.load(Ordering::Relaxed) != 0 {
            spin_loop();
        }
    }

    /// 释放 action 链表中所有 action 的内存
    ///
    /// # Safety
    ///
    /// head 必须已经从 descriptor 摘除，且所有旧遍历都已退出
    pub(super) unsafe fn reclaim_actions(head: *mut IrqAction) {
        let mut current = NonNull::new(head);

        while let Some(action) = current {
            // SAFETY: 调用方保证旧 action 链已经没有读者
            current = unsafe { action.as_ref() }.next;

            // SAFETY: 每个节点均由 Box::into_non_null_with_allocator 发布且只回收一次
            let _ = unsafe { Box::<_, Kmalloc>::from_non_null_in(action, Kmalloc::default()) };
        }
    }

    /// 创建一个占位 descriptor
    pub fn placeholder(irq: IrqNumber) -> Self {
        let data = Placeholder::data(irq);
        Self {
            irq,
            data,
            flow: Flow::Bad,
            actions: AtomicPtr::null(),
            state: Spinlock::new(State::new()),
            sharing: IrqSharing::Exclusive,
            in_progress: AtomicUsize::new(0),
        }
    }

    pub fn new(
        irq: IrqNumber,
        sharing: IrqSharing,
        domain: &'static dyn super::Domain,
        flow: Flow,
        arg: &dyn Any,
    ) -> Result<Self, IrqError> {
        let data = domain.allocate(irq, arg)?;

        Ok(Self {
            irq,
            data,
            flow,
            actions: AtomicPtr::null(),
            state: Spinlock::new(State::new()),
            sharing,
            in_progress: AtomicUsize::new(0),
        })
    }

    pub fn is_configured(&self) -> bool {
        self.data.chip().downcast_ref::<Placeholder>().is_none()
    }

    /// 替换占位数据
    pub(super) fn realloc(&mut self, sharing: IrqSharing, data: IrqData, flow: Flow) -> IrqData {
        assert!(self.data.chip().downcast_ref::<Placeholder>().is_some());

        self.flow = flow;
        self.sharing = sharing;

        mem::replace(&mut self.data, data)
    }
}

impl Drop for IrqDescriptor {
    fn drop(&mut self) {
        self.drain();

        // SAFETY: &mut self 证明 descriptor 已无外部引用，也不会再产生 action 遍历。
        unsafe { Self::reclaim_actions(*self.actions.get_mut()) };

        IrqReservation::free(self.irq);
    }
}
