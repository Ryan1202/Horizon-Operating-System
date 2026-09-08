use core::{any::Any, mem::MaybeUninit, num::NonZero, ptr::NonNull};

use alloc::{boxed::Box, sync::Arc, vec::Vec};

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
            frame::FrameNumber,
            kmalloc::Kmalloc,
            page::{Pages, options::PageAllocOptions},
        },
        thread::PreemptGuard,
    },
    lib::rust::spinlock::Spinlock,
};

static IOAPICS: IoApics = IoApics::new();

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
}

impl IoApicRegs {
    fn new(base: NonNull<u8>) -> Self {
        Self {
            select: base.cast(),
            // SAFETY: 初始化器已验证整个 MMIO 寄存器区位于持有的物理页映射内
            window: unsafe { base.byte_add(0x10).cast() },
            eoi: unsafe { base.byte_add(regs::EOI_OFFSET).cast() },
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

    fn set_mask(&self, pin: u8, masked: bool) {
        let reg = regs::REDIRECTION_TABLE + pin as usize * 2;
        let value = self.read(reg);

        self.write(reg, if masked { value | MASK } else { value & !MASK });
        let _ = self.read(reg);
    }

    fn set_redirection_entry(&self, pin: u8, entry: RedirectionEntry) {
        let low = regs::REDIRECTION_TABLE + pin as usize * 2;

        // 先停止旧路由的投递，再修改目的 CPU；新条目直到 unmask 才可投递
        self.set_mask(pin, true);
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
    gsi_end: u32,
    version: u8,
    pin_count: usize,
}

impl IoApic {
    fn new(info: &IoApicInfo, base: NonNull<u8>) -> Result<Self, IrqError> {
        let regs = Spinlock::new(IoApicRegs::new(base));
        let version = regs.lock_irqsave().read(regs::VERSION);

        let pin_count = ((version >> 16) & 0xff) as usize + 1;
        if pin_count > MAX_PINS {
            return Err(IrqError::Unsupported);
        }

        let gsi_end = info.gsi_base.get() + pin_count as u32;

        Ok(Self {
            id: info.id,
            regs,
            gsi_base: info.gsi_base.get(),
            gsi_end,
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

struct IoApicMapping {
    frame: FrameNumber,
    page: Pages,
}

struct IoApicState {
    apics: Box<[Arc<IoApic, Kmalloc>], Kmalloc>,
    // 全局所有者维持所有寄存器指针的有效性，同一物理页只映射一次。
    mappings: Vec<IoApicMapping, Kmalloc>,
}

pub struct IoApics {
    state: Spinlock<Option<IoApicState>>,
}

impl IoApics {
    const fn new() -> Self {
        Self {
            state: Spinlock::new(None),
        }
    }

    pub const fn get() -> &'static Self {
        &IOAPICS
    }

    pub fn init(&self, ioapics: &[IoApicInfo]) -> Result<(), IrqError> {
        let mut guard = self.state.lock_irqsave();
        if guard.is_some() {
            return Err(IrqError::Busy);
        }

        let mut mappings: Vec<IoApicMapping, Kmalloc> = Vec::new_in(Kmalloc::default());
        let mut apics = Box::<[Arc<IoApic, Kmalloc>], Kmalloc>::new_uninit_slice_in(
            ioapics.len(),
            Kmalloc::default(),
        );

        for (index, info) in ioapics.iter().enumerate() {
            let frame = PhysAddr::new(info.address).to_frame_number();
            let offset = info.address - PhysAddr::from_frame_number(frame).as_usize();

            let page = if let Some(mapping) = mappings.iter().find(|m| m.frame == frame) {
                &mapping.page
            } else {
                let page = PageAllocOptions::mmio(
                    frame,
                    const { NonZero::new(1).unwrap() },
                    PageCacheType::Uncached,
                )
                .allocate()
                .expect("failed to allocate IOAPIC MMIO page");

                mappings.push(IoApicMapping { frame, page });

                &mappings.last().unwrap().page
            };

            let base = unsafe { page.get_ptr::<u8>().byte_add(offset) };
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
                regs.set_redirection_entry(pin as u8, RedirectionEntry(MASK as u64));
            }
        }

        *guard = Some(IoApicState { apics, mappings });

        Ok(())
    }

    /// None 表示集合尚未初始化；空集合使用广播模式。
    pub fn all_support_eoi(&self) -> Option<bool> {
        let guard = self.state.lock_irqsave();
        let state = guard.as_ref()?;

        Some(!state.apics.is_empty() && state.apics.iter().all(|apic| apic.supports_eoi()))
    }

    fn find_chip(&self, gsi: Gsi) -> Option<(Arc<IoApic, Kmalloc>, u8)> {
        let guard = self.state.lock_irqsave();

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
    fn allocate(
        &self,
        irq: IrqNumber,
        data: &mut MaybeUninit<IrqData>,
        arg: &dyn Any,
    ) -> Result<(), IrqError> {
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
        data.write(local);

        Ok(())
    }

    fn free(&self, _data: &IrqData) {}

    fn activate(
        &self,
        _irq: IrqNumber,
        data: &IrqData,
        _affinity: &mut irq::Affinity,
    ) -> Result<(), IrqError> {
        let info = data.chip_data::<Info>();
        let parent = data.parent().ok_or(IrqError::NotFound)?;
        let route = LocalApicDomain::route(parent).ok_or(IrqError::NotFound)?;

        let chip = data
            .chip()
            .downcast_ref::<IoApic>()
            .expect("IOAPIC domain chip type mismatch");

        let regs = chip.regs.lock_irqsave();

        regs.set_redirection_entry(
            info.pin,
            RedirectionEntry::new(
                route.vector,
                route.apic_id,
                info.trigger_mode,
                info.polarity,
            ),
        );

        Ok(())
    }

    fn deactivate(&self, data: &IrqData) {
        data.chip().mask(data);

        // core 随后撤销父路由；不在此重复调用父层 deactivate/free。
    }
}
