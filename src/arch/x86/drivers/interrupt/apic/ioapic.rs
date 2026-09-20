use core::{any::Any, hint::spin_loop, num::NonZeroUsize, ptr::NonNull};

use alloc::{boxed::Box, sync::Arc};

use crate::{
    arch::{
        ArchInterrupt, PhysAddr,
        x86::kernel::interrupt::{
            apic::{self, ApicId, EoiMode, Gsi, IoApicArg, IoApicInfo, LocalApicDomain},
            vector::VectorScope,
        },
    },
    kernel::{
        interrupt::{
            Interrupt,
            irq::{self, IrqChip, IrqData, IrqError, IrqNumber, Polarity, TriggerMode},
        },
        memory::{
            PageCacheType,
            kmalloc::Kmalloc,
            page::{Pages, options::PageAllocOptions},
        },
        thread::PreemptGuard,
        topology::CpuMask,
    },
    lib::rust::{bitset::BitSet, spinlock::Spinlock},
};

static IOAPICS: IoApics = IoApics::new();

const DELIVERY_STATUS: u32 = 1 << 12;
const REMOTE_IRR: u32 = 1 << 14;
const MASK: u32 = 1 << 16;

// IOREGSEL 只有低 8 位用于标准索引，重定向项从 0x10 开始，每项占两个索引
const MAX_PINS: usize = (256 - 0x10) / 2;

mod regs {
    pub const VERSION: usize = 0x01;
    pub const REDIRECTION_TABLE: usize = 0x10;
    pub const EOI_OFFSET: usize = 0x40;
}

struct RedirectionEntry(u64);

impl RedirectionEntry {
    /// 当前仅使用 Fixed delivery 和 Physical destination，初始化时保持屏蔽
    fn new(vector: u8, destination: ApicId, trigger: TriggerMode, polarity: Polarity) -> Self {
        // 架构层保证当前 xAPIC 路由的 APIC ID 可由物理目标字段表示。
        let mut entry = vector as u64 | MASK as u64 | ((destination.get() as u64) << 56);

        if trigger == TriggerMode::Level {
            entry |= 1 << 15;
        }

        if polarity == Polarity::Low {
            entry |= 1 << 13;
        }

        Self(entry)
    }
}

struct IoApicRegs {
    select: NonNull<u32>,
    window: NonNull<u32>,
    eoi: NonNull<u32>,
    active: BitSet<[usize; MAX_PINS.div_ceil(usize::BITS as usize)]>,
}

impl IoApicRegs {
    fn new(base: NonNull<u8>) -> Self {
        Self {
            select: base.cast(),
            // SAFETY: 初始化器已验证整个 MMIO 寄存器区位于持有的物理页映射内
            window: unsafe { base.byte_add(0x10).cast() },
            eoi: unsafe { base.byte_add(regs::EOI_OFFSET).cast() },
            active: BitSet::zeroed(MAX_PINS),
        }
    }

    fn read(&self, reg: usize) -> u32 {
        unsafe {
            self.select.as_ptr().write_volatile(reg as u32);
            self.window.as_ptr().read_volatile()
        }
    }

    fn write(&self, reg: usize, value: u32) {
        unsafe {
            self.select.as_ptr().write_volatile(reg as u32);
            self.window.as_ptr().write_volatile(value);
        }
    }

    /// 设置指定 pin 的屏蔽状态，返回旧状态
    fn set_mask(&self, pin: u8, masked: bool) -> bool {
        let reg = regs::REDIRECTION_TABLE + pin as usize * 2;
        let value = self.read(reg);

        self.write(reg, if masked { value | MASK } else { value & !MASK });
        let _ = self.read(reg);

        (value & MASK) != 0
    }

    /// 设置指定 pin 的重定向表条目
    ///
    /// # Safety
    ///
    /// 设置前需要确保当前条目已被屏蔽，如果在 Level 模式下还需要确保 Remote IRR 已清零
    unsafe fn set_redirection_entry(&self, pin: u8, entry: RedirectionEntry) {
        let low = regs::REDIRECTION_TABLE + pin as usize * 2;

        self.write(low + 1, (entry.0 >> 32) as u32);
        self.write(low, entry.0 as u32 | MASK);
        let _ = self.read(low);
    }

    fn directed_eoi(&self, vector: u8) {
        // EOI 是独立的直接 MMIO 寄存器，写入的是 vector，不是 pin 或 GSI
        unsafe { self.eoi.as_ptr().write_volatile(vector as u32) };
        let _ = self.read(regs::VERSION);
    }
}

pub struct IoApic {
    id: ApicId,
    regs: Spinlock<IoApicRegs>,
    gsi_base: u32,
    version: u8,
    pin_count: u32,
}

impl IoApic {
    fn new(info: &IoApicInfo, base: NonNull<u8>) -> Result<Self, IrqError> {
        let regs = Spinlock::new(IoApicRegs::new(base));
        let version = regs.lock_irqsave().read(regs::VERSION);

        let pin_count = ((version >> 16) & 0xff) as u32 + 1;
        if pin_count > MAX_PINS as u32 {
            return Err(IrqError::Unsupported);
        }

        let gsi_base = info.gsi_base.get();

        Ok(Self {
            id: info.id,
            regs,
            gsi_base,
            version: version as u8,
            pin_count,
        })
    }

    pub const fn id(&self) -> ApicId {
        self.id
    }

    pub const fn supports_eoi(&self) -> bool {
        self.version >= 0x20
    }

    fn pin(&self, gsi: Gsi) -> Option<u8> {
        let offset = gsi.get().checked_sub(self.gsi_base)?;

        (offset < self.pin_count as u32).then_some(offset as u8)
    }

    /// 等待 Remote IRR 清零，确保所有事件完成
    ///
    /// 只在 Level 触发模式下有效，Edge 模式下无意义
    fn synchronize(&self, pin: u8) {
        let reg = &self.regs;

        loop {
            let entry = reg
                .lock_irqsave()
                .read(regs::REDIRECTION_TABLE + pin as usize * 2);

            assert!(entry & MASK != 0, "synchronize unmasked IOAPIC source");

            let level_pending = entry & REMOTE_IRR != 0;
            if entry & DELIVERY_STATUS == 0 && !level_pending {
                break;
            }

            spin_loop();
        }
    }
}

impl IrqChip for IoApic {
    fn mask(&self, data: &IrqData) {
        self.regs
            .lock_irqsave()
            .set_mask(data.chip_data::<Info>().pin, true);
    }

    fn unmask(&self, data: &IrqData) {
        let pin = data.chip_data::<Info>().pin;
        let regs = self.regs.lock_irqsave();

        regs.set_mask(pin, false);
    }

    fn ack(&self, data: &IrqData) {
        if data.chip_data::<Info>().trigger_mode == TriggerMode::Edge {
            let parent = data.parent().expect("IOAPIC without LAPIC parent");
            parent.chip().eoi(parent);
        }
    }

    fn eoi(&self, data: &IrqData) {
        if data.chip_data::<Info>().trigger_mode != TriggerMode::Level {
            return;
        }

        // 完成序列不能切换 CPU；调用父层时不持有 IOAPIC 的寄存器锁
        let _interrupt = ArchInterrupt::save_and_disable();
        let preempt = PreemptGuard::new();

        let mode = apic::current_eoi_mode(&preempt);
        let parent = data.parent().expect("IOAPIC without LAPIC parent");
        let route = LocalApicDomain::route(parent).expect("EOI without active route");

        parent.chip().eoi(parent);

        if mode == EoiMode::Directed {
            self.regs.lock_irqsave().directed_eoi(route.vector);
        }
    }
}

struct IoApicInner {
    apics: Box<[Arc<IoApic, Kmalloc>], Kmalloc>,
    _pages: Pages,
}

#[repr(transparent)]
pub struct IoApics {
    inner: Spinlock<Option<IoApicInner>>,
}

impl IoApics {
    const fn new() -> Self {
        Self {
            inner: Spinlock::new(None),
        }
    }

    pub const fn get() -> &'static Self {
        &IOAPICS
    }

    pub fn init(&self, ioapics: &[IoApicInfo]) -> Result<(), IrqError> {
        let mut guard = self.inner.lock_irqsave();
        if guard.is_some() {
            return Err(IrqError::Busy);
        }

        let mut apics = Box::<[Arc<IoApic, Kmalloc>], Kmalloc>::new_uninit_slice_in(
            ioapics.len(),
            Kmalloc::default(),
        );

        let range = ioapics
            .iter()
            .fold(None, |range, info| {
                let frame = PhysAddr::new(info.address).to_frame_number();
                let Some(range) = range else {
                    return Some(frame..frame + 1);
                };

                if frame < range.start {
                    Some(frame..range.end)
                } else if frame >= range.end {
                    Some(range.start..frame + 1)
                } else {
                    Some(range)
                }
            })
            .expect("IOAPIC list is empty");

        let count = NonZeroUsize::new(range.end.get() - range.start.get()).unwrap();
        let pages = PageAllocOptions::mmio(range.start, count, PageCacheType::Uncached)
            .allocate()
            .expect("failed to allocate IOAPIC MMIO page");

        for (index, info) in ioapics.iter().enumerate() {
            let offset = info.address - PhysAddr::from_frame_number(range.start).as_usize();

            let base = unsafe { pages.get_ptr::<u8>().byte_add(offset) };
            let apic = IoApic::new(info, base).expect("failed to initialize IOAPIC");

            let apic = Arc::new_in(apic, Kmalloc::default());
            apics[index].write(apic);
        }

        // SAFETY: 所有槽位已初始化，失败路径已清理前缀并返回
        let apics = unsafe { apics.assume_init() };

        // 校验全部完成后才编程硬件，覆盖最大下标对应的最后一个 pin
        for apic in &apics {
            let regs = apic.regs.lock_irqsave();

            for pin in 0..apic.pin_count {
                // SAFETY: 此时中断已禁用，且设置为屏蔽状态、Edge 触发模式
                unsafe { regs.set_redirection_entry(pin as u8, RedirectionEntry(MASK as u64)) };
            }
        }

        *guard = Some(IoApicInner {
            apics,
            _pages: pages,
        });

        Ok(())
    }

    /// None 表示集合尚未初始化；空集合使用广播模式
    pub fn all_support_eoi(&self) -> Option<bool> {
        let guard = self.inner.lock_irqsave();
        let state = guard.as_ref()?;

        Some(!state.apics.is_empty() && state.apics.iter().all(|apic| apic.supports_eoi()))
    }

    fn find_chip(&self, gsi: Gsi) -> Option<(Arc<IoApic, Kmalloc>, u8)> {
        let guard = self.inner.lock_irqsave();

        for apic in &guard.as_ref()?.apics {
            if let Some(pin) = apic.pin(gsi) {
                return Some((apic.clone(), pin));
            }
        }

        None
    }
}

struct Info {
    pin: u8,
    trigger_mode: TriggerMode,
    polarity: Polarity,
}

impl irq::Domain for IoApics {
    fn allocate(&self, irq: IrqNumber, arg: &dyn Any) -> Result<IrqData, IrqError> {
        let arg = arg
            .downcast_ref::<IoApicArg>()
            .ok_or(IrqError::InvalidArgument)?;

        let (chip, pin) = self
            .find_chip(arg.gsi)
            .ok_or(IrqError::InvalidHardwareIrq(arg.gsi.into_raw()))?;

        let info = Box::new_in(
            Info {
                pin,
                trigger_mode: arg.trigger_mode,
                polarity: arg.polarity,
            },
            Kmalloc::default(),
        );

        let mut local = IrqData::new(arg.gsi, Self::get(), chip, info, None);

        let scope = match arg.trigger_mode {
            TriggerMode::Edge => VectorScope::PerCpu,
            TriggerMode::Level => VectorScope::Global,
        };

        local.alloc_parent(irq, LocalApicDomain::get(), &scope)?;
        Ok(local)
    }

    unsafe fn activate(
        &self,
        irq: IrqNumber,
        data: &IrqData,
        affinity: &CpuMask,
    ) -> Result<CpuMask, IrqError> {
        let info = data.chip_data::<Info>();
        let parent = data.parent().ok_or(IrqError::NotFound)?;

        // SAFETY: 由调用者保证 irq 未激活
        let mask = unsafe { parent.domain().activate(irq, parent, affinity)? };

        let route = LocalApicDomain::route(parent).ok_or(IrqError::NotFound)?;

        let chip = data
            .chip()
            .downcast_ref::<IoApic>()
            .expect("IOAPIC domain chip type mismatch");

        let mut regs = chip.regs.lock_irqsave();

        regs.active
            .try_set(info.pin as usize)
            .ok_or(IrqError::Busy)?;
        drop(regs);

        if info.trigger_mode == TriggerMode::Level {
            chip.synchronize(info.pin);
        }

        // SAFETY: `activate` 确保当前未激活，在未激活状态下处于已屏蔽状态，并已手动同步
        unsafe {
            chip.regs.lock_irqsave().set_redirection_entry(
                info.pin,
                RedirectionEntry::new(
                    route.vector,
                    route.apic_id,
                    info.trigger_mode,
                    info.polarity,
                ),
            )
        };

        Ok(mask)
    }

    unsafe fn deactivate(&self, data: &IrqData) -> Result<(), IrqError> {
        let chip = data
            .chip()
            .downcast_ref::<IoApic>()
            .expect("IOAPIC chip mismatch");

        let pin = data.chip_data::<Info>().pin;

        assert!(
            chip.regs
                .lock_irqsave()
                .active
                .try_clear(pin as usize)
                .is_some()
        );

        if let Some(parent) = data.parent() {
            // SAFETY: 由调用者保证
            unsafe { parent.domain().deactivate(parent)? };
        }

        Ok(())
    }

    fn synchronize(&self, data: &IrqData) {
        let chip = data
            .chip()
            .downcast_ref::<IoApic>()
            .expect("IOAPIC chip mismatch");
        let info = data.chip_data::<Info>();

        if info.trigger_mode == TriggerMode::Level {
            chip.synchronize(info.pin);
        }

        if let Some(parent) = data.parent() {
            parent.domain().synchronize(parent);
        }
    }

    unsafe fn update_affinity(
        &self,
        irq: IrqNumber,
        data: &IrqData,
        affinity: &CpuMask,
    ) -> Result<CpuMask, IrqError> {
        let info = data.chip_data::<Info>();
        let parent = data.parent().ok_or(IrqError::NotFound)?;

        let chip = data
            .chip()
            .downcast_ref::<IoApic>()
            .expect("IOAPIC domain chip type mismatch");

        let masked = chip.regs.lock_irqsave().set_mask(info.pin, true);

        // 先分配新的路由
        // SAFETY: 由调用者保证 irq 已激活
        let mask = unsafe { parent.domain().update_affinity(irq, parent, affinity) };
        let route = LocalApicDomain::route(parent).ok_or(IrqError::NotFound)?;

        let regs = chip.regs.lock_irqsave();

        let mask = mask.inspect_err(|_| {
            regs.set_mask(info.pin, masked);
        })?;

        if info.trigger_mode == TriggerMode::Level {
            chip.synchronize(info.pin);
        }

        // SAFETY: 已屏蔽中断并确认在 Level 模式下 Remote IRR 已清零
        unsafe {
            regs.set_redirection_entry(
                info.pin,
                RedirectionEntry::new(
                    route.vector,
                    route.apic_id,
                    info.trigger_mode,
                    info.polarity,
                ),
            )
        };

        // 重新设置屏蔽状态，返回旧状态
        regs.set_mask(info.pin, masked);

        Ok(mask)
    }

    fn try_reclaim_route(&self, data: &IrqData) -> Result<(), IrqError> {
        let parent = data.parent().expect("IOAPIC without LAPIC parent");
        parent.domain().try_reclaim_route(parent)
    }
}
