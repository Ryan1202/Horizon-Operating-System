use super::{
    Flow, IrqDescriptor, IrqNumber, IrqSharing, action::IrqAction, assert_management,
    descriptor::Status, domain,
};
use crate::kernel::{
    interrupt::irq::{Affinity, Domain, IRQ_DESCRIPTORS, IrqError, IrqHandler},
    memory::kmalloc::Kmalloc,
};
use alloc::{boxed::Box, sync::Arc};
use core::{
    any::Any,
    hint::spin_loop,
    ptr::{NonNull, null_mut},
    sync::atomic::{AtomicBool, Ordering},
};

#[must_use = "dropping the IRQ handle unregisters its handler"]
pub struct IrqHandle {
    descriptor: Arc<IrqDescriptor, Kmalloc>,
    action: NonNull<IrqAction>,
}

// SAFETY: 管理方法独占 handle；状态锁和 Stopping 排空协议保护链表，handler 是 Send + Sync
unsafe impl Send for IrqHandle {}
unsafe impl Sync for IrqHandle {}

/// 请求使用 IRQ，只能请求预先注册的 IRQ
///
/// 成功后需通过 handle 显式 enable_irq
pub fn request_irq<'a>(
    irq: IrqNumber,
    sharing: IrqSharing,
    handler: Arc<dyn IrqHandler, Kmalloc>,
    realloc: Option<(&'static dyn Domain, Flow, &'a dyn Any)>,
) -> Result<IrqHandle, IrqError> {
    let descriptor = loop {
        let descriptor = IRQ_DESCRIPTORS.lookup(irq).ok_or(IrqError::NotFound)?;

        if descriptor.is_configured() {
            break descriptor;
        }

        let Some((domain, flow, arg)) = realloc else {
            return Err(IrqError::NotFound);
        };

        drop(descriptor);

        match IRQ_DESCRIPTORS.realloc(irq, sharing, domain, flow, arg) {
            Ok(_) | Err(IrqError::Busy) => {}
            Err(error) => {
                return Err(error);
            }
        }
    };

    let mut action = Box::<_, Kmalloc>::new_in(
        IrqAction {
            next: None,
            handler,
            enabled: AtomicBool::new(false),
        },
        Kmalloc::default(),
    );

    let mut state = loop {
        let state = descriptor.state.lock_irqsave();
        if state.status != Status::Stopping {
            break state;
        }

        drop(state);
        spin_loop();
    };

    if state.active == 0 {
        // mapping 的 sharing 固定时，首个 action 必须与其匹配
        if descriptor.sharing != sharing {
            return Err(IrqError::ExclusiveViolation);
        }
    } else if descriptor.sharing != IrqSharing::Shared || sharing != IrqSharing::Shared {
        return Err(IrqError::ExclusiveViolation);
    } else {
        let head = NonNull::new(descriptor.actions.load(Ordering::Relaxed))
            .expect("registered IRQ without an action");

        // 管理者由 state 锁串行化，旧节点的 next 不变，因此正在进行的遍历可以继续使用旧 head，
        // 新遍历则从完整初始化的新节点开始
        action.next = Some(head);

        let action = Box::into_non_null_with_allocator(action).0;

        descriptor.actions.store(action.as_ptr(), Ordering::Release);

        state.active += 1;

        drop(state);
        return Ok(IrqHandle::new(descriptor, action));
    }

    state.active = 1;

    assert!(descriptor.actions.load(Ordering::Relaxed).is_null());

    let action = Box::into_non_null_with_allocator(action).0;
    descriptor.actions.store(action.as_ptr(), Ordering::Release);

    // 解锁前修改状态为 Stopping 防止其他 `request_irq` 修改
    state.status = Status::Stopping;
    drop(state);

    // action 必须先发布，activate 的最终提交可能立即允许硬件开始投递。
    let mut affinity = Affinity::Auto;
    if let Err(error) = domain::activate(irq, &descriptor.data, &mut affinity) {
        let mut state = descriptor.state.lock_irqsave();
        let removed = descriptor.actions.swap(null_mut(), Ordering::AcqRel);

        assert_eq!(removed, action.as_ptr());
        assert_eq!(state.active, 1);

        state.active = 0;
        state.status = Status::Inactive;
        drop(state);

        // SAFETY: 激活失败后没有 handle，且 Stopping 阻止了 action 遍历。
        let _ = unsafe { Box::<_, Kmalloc>::from_non_null_in(action, Kmalloc::default()) };

        return Err(error);
    }

    descriptor.state.lock_irqsave().status = Status::Disabled;

    Ok(IrqHandle::new(descriptor, action))
}

impl IrqHandle {
    pub(super) fn new(descriptor: Arc<IrqDescriptor, Kmalloc>, action: NonNull<IrqAction>) -> Self {
        Self { descriptor, action }
    }

    fn descriptor(&self) -> &IrqDescriptor {
        // SAFETY: handle 仅引用已配置、地址稳定且常驻的 descriptor
        self.descriptor.as_ref()
    }

    pub fn irq(&self) -> IrqNumber {
        self.descriptor().irq
    }

    /// 软件开启 IRQ，如果硬件已屏蔽则解除硬件屏蔽
    pub fn enable_irq(&mut self) {
        self.set_enabled(true);

        let descriptor = self.descriptor();
        let data = &descriptor.data;
        let mut state = descriptor.state.lock_irqsave();

        if state.status == Status::Disabled {
            // 如果当前已关闭则可以直接开启
            state.status = Status::Enabled;
            if descriptor.flow != Flow::Simple {
                data.chip().unmask(data);
            }
        }
    }

    /// 软件关闭 IRQ，如果所有 handler 都关闭则硬件屏蔽
    pub fn disable_irq(&mut self) {
        self.set_enabled(false);

        let descriptor = self.descriptor();
        let data = &descriptor.data;
        let mut state = descriptor.state.lock_irqsave();

        if state.status == Status::Enabled {
            // 如果当前已开启则需要所有 action 都关闭才能屏蔽
            if descriptor.sharing == IrqSharing::Exclusive {
                state.status = Status::Disabled;
                if descriptor.flow != Flow::Simple {
                    data.chip().mask(data);
                }
            } else {
                let mut all_disabled = true;
                let mut current = unsafe { descriptor.actions.load(Ordering::Relaxed).as_ref() };
                while let Some(next) = current {
                    if next.enabled.load(Ordering::Acquire) {
                        all_disabled = false;
                        break;
                    }
                    current = next.next.map(|ptr| unsafe { ptr.as_ref() });
                }

                if all_disabled {
                    state.status = Status::Disabled;
                    if descriptor.flow != Flow::Simple {
                        data.chip().mask(data);
                    }
                }
            }
        }
    }

    fn set_enabled(&mut self, enabled: bool) {
        assert_management();

        unsafe { self.action.as_ref() }
            .enabled
            .store(enabled, Ordering::Relaxed);
    }
}

impl Drop for IrqHandle {
    fn drop(&mut self) {
        assert_management();

        self.disable_irq();

        let descriptor = self.descriptor();
        {
            let mut state = descriptor.state.lock_irqsave();
            assert!(state.active > 0, "IRQ action count underflow");

            state.active -= 1;
            if state.active != 0 {
                // 非最后一个节点 disabled 留在链中，避免为普通 hardirq 遍历增加 reader 字段或回收协议
                return;
            }

            assert!(matches!(state.status, Status::Enabled | Status::Disabled));
            state.status = Status::Stopping;
        }

        let data = &descriptor.data;
        if descriptor.flow != Flow::Simple {
            data.chip().mask(data);
        }

        descriptor.drain();

        // 封闭旧 flow 在第一次 mask 前已经进入、并在退出时恢复投递的窗口。
        if descriptor.flow != Flow::Simple {
            data.chip().mask(data);
        }

        domain::synchronize(data);
        domain::deactivate(data);

        let actions = {
            let mut state = descriptor.state.lock_irqsave();
            assert!(state.status == Status::Stopping && state.active == 0);

            let actions = descriptor.actions.swap(null_mut(), Ordering::AcqRel);

            state.status = Status::Inactive;
            actions
        };

        // SAFETY: Stopping 已阻止新遍历，完整 flow 已排空，head 也已摘除
        unsafe { IrqDescriptor::reclaim_actions(actions) };
    }
}
