use core::{
    cell::OnceCell,
    num::NonZero,
    ptr::NonNull,
};

use alloc::vec::Vec;

use crate::{
    arch::{
        ArchInterrupt, PhysAddr,
        x86::kernel::{
            interrupt::{
                apic::{ApicId, LocalApic},
                vector::{ERROR_VECTOR, SPURIOUS_VECTOR, VectorManager},
            },
        },
    },
    cpu_local,
    kernel::{
        interrupt::{Interrupt, irq::IrqError},
        memory::{
            PageCacheType,
            frame::FrameNumber,
            kmalloc::Kmalloc,
            page::{Pages, options::PageAllocOptions},
            percpu::PerCpuInit,
        },
        thread::PreemptGuard,
        topology::CpuId,
    },
    lib::rust::spinlock::Spinlock,
};

const SOFTWARE_ENABLE: u32 = 1 << 8;
const LVT_MASK: u32 = 1 << 16;

mod offsets {
    pub const ID: usize = 0x20;
    pub const VERSION: usize = 0x30;
    pub const TPR: usize = 0x80;
    pub const EOI: usize = 0xb0;
    pub const SVR: usize = 0xf0;
    pub const ESR: usize = 0x280;
}

/// LVT 源与 IDT vector 是两种编号，不能用 vector 索引寄存器
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum LvtEntry {
    Timer,
    Thermal,
    Performance,
    Lint0,
    Lint1,
    Error,
    Cmci,
}

impl LvtEntry {
    const fn offset(self) -> usize {
        match self {
            Self::Timer => 0x320,
            Self::Thermal => 0x330,
            Self::Performance => 0x340,
            Self::Lint0 => 0x350,
            Self::Lint1 => 0x360,
            Self::Error => 0x370,
            Self::Cmci => 0x2f0,
        }
    }

    const fn supported(self, max_lvt: u32) -> bool {
        match self {
            Self::Timer | Self::Lint0 | Self::Lint1 => true,
            Self::Error => max_lvt >= 3,
            Self::Performance => max_lvt >= 4,
            Self::Thermal => max_lvt >= 5,
            Self::Cmci => max_lvt >= 6,
        }
    }
}

// 物理页映射属于全局；同一地址的 MMIO 访问仍由硬件指向执行访问的 CPU。
// 仅初始化时查找或插入，持有至内核结束，不随某个 CPU 的实例释放。
static LAPIC_MAPPINGS: Spinlock<Vec<LapicMapping, Kmalloc>> =
    Spinlock::new(Vec::new_in(Kmalloc::new()));

struct LapicMapping {
    frame: FrameNumber,
    page: Pages,
}

struct LapicMmioRegs {
    // 只借用全局映射中的寄存器视图，不拥有 Pages。
    base: NonNull<u32>,
}

impl LapicMmioRegs {
    fn new(address: PhysAddr) -> Result<Self, IrqError> {
        let frame = address.to_frame_number();
        let offset = address.as_usize() - PhysAddr::from_frame_number(frame).as_usize();

        // 查找与建立映射串行化，避免多个 CPU 同时映射同一物理页。
        let mut mappings = LAPIC_MAPPINGS.lock_irqsave();
        if let Some(mapping) = mappings.iter().find(|mapping| mapping.frame == frame) {
            return Ok(Self {
                // SAFETY: 寄存器基址位于此物理页内；全局所有者不会释放映射。
                base: unsafe { mapping.page.get_ptr::<u8>().byte_add(offset).cast() },
            });
        }

        let page = PageAllocOptions::mmio(
            frame,
            const { NonZero::new(1).unwrap() },
            PageCacheType::Uncached,
        )
        .allocate()?;

        // 移动 Pages 或扩容 Vec 不改变页映射的虚拟地址。
        let base = unsafe { page.get_ptr::<u8>().byte_add(offset).cast() };
        mappings.push(LapicMapping { frame, page });
        Ok(Self { base })
    }

    fn read(&self, offset: usize) -> u32 {
        // SAFETY: 偏移仅来自本模块的 LAPIC 寄存器，位于全局持有的映射内
        unsafe { self.base.as_ptr().byte_add(offset).read_volatile() }
    }

    fn write(&self, offset: usize, value: u32) {
        unsafe { self.base.as_ptr().byte_add(offset).write_volatile(value) };
        // 读回同一设备，保证寄存器写入完成后再进行下一步操作
        let _ = self.read(offsets::ID);
    }
}

pub struct LocalXApic {
    mmio: LapicMmioRegs,
    max_lvt_entry: u32,
}

impl !Send for LocalXApic {}
impl !Sync for LocalXApic {}

struct LocalState(OnceCell<LocalXApic>);

// per-CPU 模板中的 OnceCell 为空；各 CPU 初始化自己的实例后只读访问。
unsafe impl PerCpuInit for LocalState {}

cpu_local! {
    static LAPIC: LocalState = LocalState(OnceCell::new());
}

impl LocalXApic {
    /// 初始化当前 CPU，保持软件禁用和所有 LVT 屏蔽
    ///
    /// # Safety
    /// 架构层须已确认并启用当前 CPU 的 xAPIC 模式，address 为其 MMIO 基址。
    pub(crate) unsafe fn init_current(address: PhysAddr) -> Result<(), IrqError> {
        let _interrupt = ArchInterrupt::save_and_disable();
        let preempt = PreemptGuard::new();

        let local = LAPIC.get_local(&preempt);
        if local.0.get().is_some() {
            return Err(IrqError::Busy);
        }
        let mmio = LapicMmioRegs::new(address)?;

        let svr = mmio.read(offsets::SVR);
        mmio.write(offsets::SVR, svr & !SOFTWARE_ENABLE);

        let max_lvt_entry = (mmio.read(offsets::VERSION) >> 16) & 0xff;

        // Error 先配置合法向量，其余源清除固件遗留的 delivery mode。
        for entry in [
            LvtEntry::Error,
            LvtEntry::Timer,
            LvtEntry::Lint0,
            LvtEntry::Lint1,
            LvtEntry::Performance,
            LvtEntry::Thermal,
            LvtEntry::Cmci,
        ] {
            if entry.supported(max_lvt_entry) {
                mmio.write(entry.offset(), LVT_MASK | ERROR_VECTOR as u32);
            }
        }

        if LvtEntry::Error.supported(max_lvt_entry) {
            mmio.write(offsets::ESR, 0);
            let _ = mmio.read(offsets::ESR);
        }

        mmio.write(offsets::TPR, 0);
        // 使用广播 EOI；保留其它位，关闭软件启用和 EOI broadcast suppression。
        mmio.write(
            offsets::SVR,
            (svr & !(0xff | SOFTWARE_ENABLE | (1 << 12))) | SPURIOUS_VECTOR as u32,
        );

        local
            .0
            .set(Self {
                mmio,
                max_lvt_entry,
            })
            .map_err(|_| IrqError::Busy)
    }

    /// 闭包执行期间固定 CPU 并关闭中断，寄存器引用不能逃逸
    pub fn with_current<T>(f: impl FnOnce(&Self) -> T) -> Result<T, IrqError> {
        let _interrupt = ArchInterrupt::save_and_disable();
        let preempt = PreemptGuard::new();

        let local = LAPIC.get_local(&preempt);
        Ok(f(local.0.get().ok_or(IrqError::NotFound)?))
    }

    /// 启用当前 LAPIC，并将当前 CPU 纳入向量分配候选
    ///
    /// # Safety
    ///
    /// cpu 必须是当前逻辑 CPU；IDT 必须已安装所有可分配向量及 error/spurious 入口。
    /// 旧的 C APIC 驱动不能再操作此 LAPIC
    pub unsafe fn enable_current(cpu: CpuId) -> Result<(), IrqError> {
        Self::with_current(|lapic| {
            VectorManager::get().register_cpu(cpu, lapic.id())?;
            lapic.mmio.write(
                offsets::SVR,
                lapic.mmio.read(offsets::SVR) | SOFTWARE_ENABLE,
            );
            Ok(())
        })?
    }

    pub fn version(&self) -> u8 {
        (self.mmio.read(offsets::VERSION) & 0xff) as u8
    }

    pub fn supports(&self, entry: LvtEntry) -> bool {
        entry.supported(self.max_lvt_entry)
    }

    pub fn mask(&self, entry: LvtEntry) -> Result<(), IrqError> {
        self.check_entry(entry)?;
        self.mmio
            .write(entry.offset(), self.mmio.read(entry.offset()) | LVT_MASK);
        Ok(())
    }

    /// 对应 vector 必须已由调用方安装入口并绑定 handler。
    pub fn unmask(&self, entry: LvtEntry) -> Result<(), IrqError> {
        self.check_entry(entry)?;
        let value = self.mmio.read(entry.offset());
        if entry != LvtEntry::Error && value as u8 == ERROR_VECTOR {
            // 初始化占位向量不能作为普通 LVT 的投递目标。
            return Err(IrqError::InvalidArgument);
        }
        self.mmio.write(entry.offset(), value & !LVT_MASK);
        Ok(())
    }

    fn check_entry(&self, entry: LvtEntry) -> Result<(), IrqError> {
        if self.supports(entry) {
            Ok(())
        } else {
            Err(IrqError::Unsupported)
        }
    }

    pub fn read_and_clear_error(&self) -> Result<u32, IrqError> {
        self.check_entry(LvtEntry::Error)?;
        // 写 ESR 将内部错误状态锁存到可读寄存器，并重置内部状态。
        self.mmio.write(offsets::ESR, 0);
        Ok(self.mmio.read(offsets::ESR))
    }

    /// 错误入口独立使用；不能再经过普通 flow 重复 EOI。
    pub fn handle_error(&self) -> Result<u32, IrqError> {
        let error = self.read_and_clear_error()?;
        self.eoi();
        Ok(error)
    }

    /// 伪中断不设置 ISR，因此不能发送 EOI。
    pub fn handle_spurious(&self) {}
}

impl LocalApic for LocalXApic {
    fn id(&self) -> ApicId {
        ApicId::new(self.mmio.read(offsets::ID) >> 24)
    }

    /// 仅允许在屏蔽状态下修改向量，屏蔽位由 mask/unmask 单独控制。
    fn set_lvt_entry(&self, entry: LvtEntry, vector: u8) -> Result<(), IrqError> {
        self.check_entry(entry)?;
        if vector < 0x30
            || vector == 0x80
            || vector == SPURIOUS_VECTOR
            || (entry == LvtEntry::Error) != (vector == ERROR_VECTOR)
        {
            return Err(IrqError::InvalidArgument);
        }
        let value = self.mmio.read(entry.offset());
        if value & LVT_MASK == 0 {
            return Err(IrqError::Busy);
        }
        self.mmio
            .write(entry.offset(), (value & !0xff) | vector as u32);
        Ok(())
    }

    fn eoi(&self) {
        self.mmio.write(offsets::EOI, 0);
    }
}
