use core::{any::Any, mem::MaybeUninit, num::NonZero, ptr::NonNull, range::Range};

use alloc::{boxed::Box, sync::Arc};

use crate::{
    arch::{
        PhysAddr,
        x86::kernel::interrupt::apic::{ApicId, Gsi, IoApicArg, IoApicInfo},
    },
    kernel::{
        interrupt::irq::{self, IrqChip, IrqData, IrqError, IrqNumber, Polarity, TriggerMode},
        memory::{
            PageCacheType, frame::FrameNumber, kmalloc::Kmalloc, page::options::PageAllocOptions,
        },
    },
    lib::rust::spinlock::Spinlock,
};

static IOAPICS: IoApics = IoApics::new();

mod regs {
    pub const ID: usize = 0x00;
    pub const VERSION: usize = 0x01;
    pub const ARBITRATION_ID: usize = 0x02;
    pub const REDIRECTION_TABLE: usize = 0x10;
}

struct IoApicVersion {
    version: u8,
    max_redir_entry: u8,
}

enum Destination {
    Physical(u8),
    Logical(u8),
}

enum DeliveryMode {
    Fixed,
    LowestPriority,
    SMI,
    NMI,
    INIT,
    ExtINT,
}

struct RedirectionEntry(u64);

impl RedirectionEntry {
    const fn masked() -> Self {
        Self(1 << 16)
    }

    const fn new(
        vector: u8,
        delivery_mode: DeliveryMode,
        trigger_mode: TriggerMode,
        polarity: Polarity,
        destination: Destination,
    ) -> Self {
        let mut entry = 0u64;

        entry |= vector as u64;

        entry |= match delivery_mode {
            DeliveryMode::Fixed => 0b000 << 8,
            DeliveryMode::LowestPriority => 0b001 << 8,
            DeliveryMode::SMI => 0b010 << 8,
            DeliveryMode::NMI => 0b100 << 8,
            DeliveryMode::INIT => 0b101 << 8,
            DeliveryMode::ExtINT => 0b111 << 8,
        };

        entry |= match polarity {
            Polarity::High => 0 << 13,
            Polarity::Low => 1 << 13,
        };

        entry |= match trigger_mode {
            TriggerMode::Edge => 0 << 15,
            TriggerMode::Level => 1 << 15,
        };

        entry |= match destination {
            Destination::Physical(apic_id) => (apic_id as u64) << 56,
            Destination::Logical(logical_id) => (logical_id as u64) << 56 | (1 << 11),
        };

        Self(entry)
    }

    const fn get(&self) -> u64 {
        self.0
    }
}

pub struct IoApicRegs {
    select: NonNull<u32>,
    window: NonNull<u32>,
}

impl IoApicRegs {
    pub const fn new(base: NonNull<u32>) -> Self {
        let select = base;
        let window = unsafe { base.byte_add(0x10) };
        Self { select, window }
    }

    const fn read(&self, reg: usize) -> u32 {
        unsafe {
            self.select.as_ptr().write_volatile(reg as u32);
            self.window.as_ptr().read_volatile()
        }
    }

    const fn write(&self, reg: usize, value: u32) {
        unsafe {
            self.select.as_ptr().write_volatile(reg as u32);
            self.window.as_ptr().write_volatile(value);
        }
    }

    const fn read_version(&self) -> IoApicVersion {
        let version = self.read(regs::VERSION);
        IoApicVersion {
            version: (version & 0xFF) as u8,
            max_redir_entry: ((version >> 16) & 0xFF) as u8,
        }
    }

    const fn set_redirection_entry(&self, index: u8, entry: RedirectionEntry) {
        let low_index = regs::REDIRECTION_TABLE + (index as usize) * 2;
        let high_index = low_index + 1;

        self.write(low_index, entry.get() as u32);
        self.write(high_index, (entry.get() >> 32) as u32);
    }
}

pub struct IoApic {
    id: ApicId,
    regs: Spinlock<IoApicRegs>,
    gsi_base: Gsi,
    version: u8,
    max_redir_entry: u8,
}

impl IoApic {
    pub const fn new(info: &IoApicInfo, base: NonNull<u32>) -> Self {
        let regs = IoApicRegs::new(base);
        let version_info = regs.read_version();
        let regs = Spinlock::new(regs);
        Self {
            id: info.id,
            regs,
            gsi_base: info.gsi_base,
            version: version_info.version,
            max_redir_entry: version_info.max_redir_entry,
        }
    }
}

impl IrqChip for IoApic {
    fn mask(&self, irq: IrqNumber, data: &IrqData) {
        todo!()
    }

    fn unmask(&self, irq: IrqNumber, data: &IrqData) {
        todo!()
    }

    fn ack(&self, irq: IrqNumber, data: &IrqData) {
        todo!()
    }

    fn eoi(&self, irq: IrqNumber, data: &IrqData) {
        todo!()
    }
}

pub struct IoApics {
    apics: Spinlock<Option<Box<[Arc<IoApic, Kmalloc>], Kmalloc>>>,
}

impl IoApics {
    pub const fn new() -> Self {
        Self {
            apics: Spinlock::new(None),
        }
    }

    pub fn init(&self, ioapics: Option<&[IoApicInfo]>) {
        let ioapics = ioapics.unwrap_or_default();

        let range = mmio_range(&ioapics);
        let start_addr = PhysAddr::from_frame_number(range.start).as_usize();
        let count = NonZero::new(range.end.get() - range.start.get()).unwrap();

        let page = PageAllocOptions::mmio(range.start, count, PageCacheType::Uncached)
            .allocate()
            .expect("Failed to allocate MMIO pages for I/O APICs");
        let addr = page.get_ptr();

        let mut apics = Box::new_uninit_slice_in(ioapics.len(), Kmalloc::default());

        for (apic, info) in apics.iter_mut().zip(ioapics) {
            let offset = info.address - start_addr;

            let ioapic = IoApic::new(info, unsafe { addr.byte_add(offset) });
            let ioapic = Arc::new_in(ioapic, Kmalloc::default());

            apic.write(ioapic);
        }

        let apics = unsafe { apics.assume_init() };

        for apic in &apics {
            for i in 0..apic.max_redir_entry {
                apic.regs
                    .lock()
                    .set_redirection_entry(i, RedirectionEntry::masked());
            }
        }

        unsafe {
            self.apics.init_with(|v| {
                *v = Some(apics);
            })
        };
    }

    fn find_chip(&self, gsi: Gsi) -> Option<(Arc<IoApic, Kmalloc>, u8)> {
        let apics = self.apics.lock();
        let apics = apics.as_ref()?;

        for apic in apics.iter() {
            if let Some(pin) = apic.pin(gsi) {
                return Some((apic.clone(), pin));
            }
        }

        None
    }
}

fn mmio_range(ioapics: &[IoApicInfo]) -> Range<FrameNumber> {
    let frame_number = PhysAddr::new(ioapics[0].address).to_frame_number();
    let mut range = Range::from(frame_number..(frame_number + 1));
    for info in ioapics.iter().skip(1) {
        let frame_number = PhysAddr::new(info.address).to_frame_number();
        range = (range.start.min(frame_number)..range.end.max(frame_number + 1)).into();
    }

    range
}

impl IoApic {
    fn index(&self, irq: &IrqData) -> u8 {
        let irq = irq.raw_irq().get();
        assert!(irq >= self.gsi_base.get());
        let index = (irq - self.gsi_base.get()) as u8;
        assert!(index < self.max_redir_entry);
        index
    }

    const fn pin(&self, gsi: Gsi) -> Option<u8> {
        let gsi = gsi.get();

        let base = self.gsi_base.get();
        if gsi < base || gsi >= base + self.max_redir_entry as u32 {
            return None;
        }
        let pin = (gsi - base) as u8;
        Some(pin)
    }
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

        let info = Info {
            pin,
            trigger_mode: arg.trigger_mode,
            polarity: arg.polarity,
            entry: None,
        };
        let chip_data = Box::new_in(info, Kmalloc::default());

        data.write(IrqData::new(arg.gsi, &IOAPICS, chip, chip_data, None));

        Ok(())
    }

    fn free(&self, data: &IrqData) {
        todo!()
    }

    fn activate(
        &self,
        irq: IrqNumber,
        data: &IrqData,
        affinity: &mut irq::Affinity,
    ) -> Result<(), IrqError> {
        todo!()
    }

    fn deactivate(&self, data: &IrqData) {
        todo!()
    }
}

struct Info {
    pin: u8,

    trigger_mode: TriggerMode,
    polarity: Polarity,

    entry: Option<RedirectionEntry>,
}
