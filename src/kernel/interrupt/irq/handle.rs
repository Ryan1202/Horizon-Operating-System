//! handle 只管理自己的 action；最后一个 action 注销才停用 domain。

use super::{
    IrqDescriptor, IrqNumber, action::IrqAction, assert_management, descriptor::Status, domain,
};
use crate::kernel::memory::kmalloc::Kmalloc;
use alloc::boxed::Box;
use core::ptr::NonNull;

#[must_use = "dropping the IRQ handle unregisters its handler"]
pub struct IrqHandle {
    descriptor: NonNull<IrqDescriptor>,
    action: NonNull<IrqAction>,
}

// SAFETY: 管理方法独占 handle；状态锁和 Stopping 排空协议保护链表，handler 是 Send + Sync
unsafe impl Send for IrqHandle {}
unsafe impl Sync for IrqHandle {}

impl IrqHandle {
    pub(super) fn new(descriptor: NonNull<IrqDescriptor>, action: NonNull<IrqAction>) -> Self {
        Self { descriptor, action }
    }

    fn descriptor(&self) -> &IrqDescriptor {
        // SAFETY: handle 仅引用已配置、地址稳定且常驻的 descriptor
        unsafe { self.descriptor.as_ref() }
    }

    pub fn irq(&self) -> IrqNumber {
        self.descriptor().irq
    }

    pub fn enable_irq(&mut self) {
        self.set_enabled(true);
    }

    pub fn disable_irq(&mut self) {
        self.set_enabled(false);
    }

    fn set_enabled(&mut self, enabled: bool) {
        assert_management();
        let descriptor = self.descriptor();
        let mut state = descriptor.lock_stable();
        // SAFETY: 此 handle 的节点仍在链表中；enabled 的读写由状态锁保护
        if unsafe { self.action.as_ref() }.enabled == enabled {
            return;
        }
        state.freeze(descriptor);
        drop(state);
        descriptor.drain();

        {
            let _state = descriptor.state.lock_irqsave();
            // SAFETY: Stopping 排除了其它管理者，旧遍历已排空，无存活的节点共享引用
            let mut action = self.action;
            unsafe { action.as_mut().enabled = enabled };
        }
        descriptor.finish_update();
    }
}

impl Drop for IrqHandle {
    fn drop(&mut self) {
        assert_management();
        let descriptor = self.descriptor();

        descriptor.lock_stable().freeze(descriptor);
        descriptor.drain();

        let mut state = descriptor.state.lock_irqsave();

        // SAFETY: 本 handle 独占其节点的注销权，全部遍历已排空
        let next = unsafe { self.action.as_ref() }.next;
        if state.head == Some(self.action) && next.is_none() {
            // 最后一个节点在停用完成前继续占用注册位置
            drop(state);

            domain::synchronize(&descriptor.data);
            descriptor.state.lock_irqsave().status = Status::Inactive;
            domain::deactivate(&descriptor.data);

            let mut state = descriptor.state.lock_irqsave();
            state.head = None;
            state.pending = false;
        } else {
            if state.head == Some(self.action) {
                state.head = next;
            } else {
                let mut previous = state.head.expect("missing IRQ action");

                loop {
                    // SAFETY: 独占更新阶段且遍历已排空，节点引用不逃逸状态锁
                    let current = unsafe { previous.as_mut() };
                    if current.next == Some(self.action) {
                        current.next = next;
                        break;
                    }
                    previous = current.next.expect("IRQ action ownership mismatch");
                }
            }
            drop(state);

            descriptor.finish_update();
        }

        // SAFETY: 节点已摘除且不再被分发引用；稳定状态恢复后在锁外析构。
        unsafe {
            drop(Box::<_, Kmalloc>::from_raw_in(
                self.action.as_ptr(),
                Kmalloc::default(),
            ));
        }
    }
}
