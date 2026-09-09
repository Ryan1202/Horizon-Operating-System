//! 一个 handle 拥有一个 Exclusive action。注销不释放常驻配置

use super::{
    IrqDescriptor, IrqNumber, action::IrqAction, descriptor::Status, sync::assert_management,
};
use crate::kernel::memory::kmalloc::Kmalloc;
use alloc::boxed::Box;
use core::{hint::spin_loop, ptr::NonNull, sync::atomic::Ordering};

#[must_use = "dropping the IRQ handle unregisters its handler"]
pub struct IrqHandle {
    descriptor: NonNull<IrqDescriptor>,
    action: NonNull<IrqAction>,
}

// SAFETY: action 身份由 handle 独占，链表访问由状态锁串行化；handler 是 Send + Sync
unsafe impl Send for IrqHandle {}
unsafe impl Sync for IrqHandle {}

impl IrqHandle {
    pub(super) fn new(descriptor: NonNull<IrqDescriptor>, action: NonNull<IrqAction>) -> Self {
        Self { descriptor, action }
    }

    pub fn irq(&self) -> IrqNumber {
        // SAFETY: 注册只返回已配置 descriptor，其地址稳定且本阶段不回收
        unsafe { self.descriptor.as_ref().irq }
    }
}

impl Drop for IrqHandle {
    fn drop(&mut self) {
        assert_management();

        // SAFETY: handle 由成功注册构造，descriptor 在注销期间保持存活
        let descriptor = unsafe { self.descriptor.as_ref() };

        {
            let mut state = descriptor.state.lock_irqsave();
            assert_eq!(
                state.head,
                Some(self.action),
                "IRQ action ownership mismatch"
            );

            let active = state.status == Status::Active;
            state.status = Status::Stopping;
            state.pending = false;

            if active && descriptor.flow != super::Flow::Simple {
                descriptor.data.chip().mask(&descriptor.data);
            }
        }

        loop {
            while descriptor.in_progress.load(Ordering::Relaxed) {
                spin_loop();
            }

            let mut state = descriptor.state.lock_irqsave();
            // 锁外观察只用于减少锁竞争；此处复核并取得执行者收尾的可见性。
            if descriptor.in_progress.load(Ordering::Relaxed) {
                continue;
            }

            state.head = None;
            state.status = Status::Inactive;
            break;
        }

        // 私有状态的 drop 不持有任何 IRQ 锁
        // SAFETY: Stopping 阻止新执行，已有执行者已完成全部收尾，唯一节点已摘下。
        unsafe {
            drop(Box::<_, Kmalloc>::from_raw_in(
                self.action.as_ptr(),
                Kmalloc::default(),
            ))
        };
    }
}
