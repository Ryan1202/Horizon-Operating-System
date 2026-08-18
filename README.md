<h1 align="center">Horizon 操作系统</h1>

<p align="center">
  <a href="https://github.com/Ryan1202/Horizon-Operating-System">
    <img src="https://img.shields.io/github/stars/Ryan1202/Horizon-Operating-System.svg?logo=GitHub" alt="GitHub stars" />
  </a>
  <a href="https://github.com/Ryan1202/Horizon-Operating-System">
    <img src="https://img.shields.io/github/forks/Ryan1202/Horizon-Operating-System.svg?logo=GitHub" alt="GitHub forks" />
  </a>
  <a href="https://github.com/Ryan1202/Horizon-Operating-System/blob/master/LICENSE">
    <img src="https://img.shields.io/github/license/Ryan1202/Horizon-Operating-System.svg" alt="License" />
  </a>
</p>

<p align="center">
  面向 x86_64 的实验性操作系统，使用 C 与 Rust 共同实现。<br />
  项目仍处于开发阶段，接口和内部设计可能持续调整。
</p>

## 当前状态

Horizon 已从早期的 32 位内核逐步迁移为 x86_64 高半区内核，并正在将内存管理、线程与同步等核心子系统迁移到 Rust。当前已实现或正在迭代的主要模块包括：

- GRUB2 / Multiboot2 引导和 x86_64 高半区内核；
- Rust/C 混合构建、FFI 接口与自动模块配置工具；
- Buddy、SLUB、页表、`kmalloc`、`vmalloc` 和 DMA 等内存管理设施；
- 内核线程、调度器、WaitQueue、Completion 与抢占管理；
- 静态及动态 per-CPU 基础设施；
- PCI、ISA、ATA/IDE、UHCI、RTL8139、SB16、VESA、串口和 PS/2 等实验性驱动；
- FAT、对象树，以及实验性的 IPv4、ARP、ICMP、UDP、TCP 和 DHCP 支持。

这些功能仍以架构验证和持续重构为主，不代表完整、稳定或生产可用的操作系统实现。

## Roadmap

- [x] GRUB2 / Multiboot2
- [ ] Boot Abstraction / BootInfo
  - [ ] Kernel Core
  - [ ] Native UEFI Loader
- [x] per-CPU
  - [x] static
  - [x] dynamic
- [ ] ACPI
  - [ ] Tables：RSDP / RSDT / XSDT / Table Manager
  - [ ] AML：DSDT / SSDT / Namespace / Interpreter
  - [ ] OS Services：OperationRegion / Mutex / Global Lock / EC
  - [ ] Device：Enumeration / Identification / Resources / PCI Routing
  - [ ] Events：SCI / Fixed Events / GPE / Notify
  - [ ] Power：System Sleep / Device Power / CPU Power / Thermal
  - [ ] Platform：MADT / FADT / HPET / MCFG / SRAT / SLIT / DMAR
  - [ ] Compatibility：QEMU / OVMF / Real Hardware

- 多核

  - [x] APIC
  - [ ] x2APIC

  - [ ] SMP
  - [ ] IPI
  - [ ] TLB shootdown

- [ ] Scheduler framework
  - [ ] CFS-like
  - [ ] EEVDF
- [ ] PCIe
  - [ ] MSI-X
  - [ ] DMA
- VirtIO
  - [ ] VirtIO blk
  - [ ] VirtIO net
- [ ] NVMe

- USB

  - [x] UHCI

  - [ ] OHCI

  - [ ] EHCI

  - [ ] XHCI

  - [ ] USB Core

- [ ] SCSI
- [ ] VFS
- [ ] cache
- [ ] ext2
  - [ ] ext 3 / 4
- [ ] Network subsystem v2
  - [ ] 新 TCP-IP 协议栈

路线图表示计划中的依赖顺序和演进方向，不承诺具体发布时间。Native UEFI Loader 与 Kernel Core 将在统一的 BootInfo 边界下并行演进。

## 构建环境

构建需要以下工具：

- C 编译器
  - GCC or
  - Clang/LLVM 和 LLD；

- NASM；
- GNU Make；
- Python 3；
- Rust (Rustup)；
- GRUB2 工具及对应的 `i386-pc` 模块；
- QEMU（运行时使用 `qemu-system-x86_64`）。

### GRUB

首次构建前需要准备 GRUB：

- **Linux**：安装发行版提供的 GRUB2、`i386-pc` 模块及相关工具。安装脚本默认从 `/usr/lib/grub` 查找模块；
- **macOS**：通过 Homebrew 安装交叉编译版本：

  ```shell
  brew install i686-elf-grub
  ```

- **Windows**：下载并解压 [GRUB 2.12 for Windows](https://ftp.gnu.org/gnu/grub/grub-2.12-for-windows.zip)，运行安装脚本时按提示输入 GRUB 路径。

## 快速开始

克隆仓库并初始化子模块：

```shell
git clone --recursive https://github.com/Ryan1202/Horizon-Operating-System.git
cd Horizon-Operating-System
```

如果已经克隆过仓库：

```shell
git submodule update --init --recursive
```

首次使用时创建包含 GRUB 的虚拟硬盘，然后构建并运行：

```shell
make tool
make hd
make run
```

`make` 会通过 configurator 扫描各目录的 `config.toml`，生成内核构建目录、Cargo 配置。内核输出为 `src/build/kernel.elf`，并会被复制到 `hd0.img`。

## 常用命令

| 命令 | 说明 |
| --- | --- |
| `make` | 构建内核与用户态库 |
| `make tool` | 构建 imagetool 和 configurator |
| `make hd` | 创建包含 GRUB 的虚拟硬盘 |
| `make writehd` | 将 `disk/` 内容复制到虚拟硬盘 |
| `make run` | 构建并在 QEMU 中运行 |
| `make run_dbg` | 启动 QEMU 并等待 GDB 连接（端口 1234） |
| `make lib` | 构建用户态库 |
| `make app` | 构建用户态应用 |
| `make clean` | 清理常规构建产物 |
| `make clean_all` | 清理全部构建产物及工具 |

QEMU 网络默认将宿主机 TCP `5555` 端口转发到虚拟机 `80` 端口，并将数据包记录到 `dump.pcap`。不同宿主平台的音频配置位于 `qemu-linux.cfg`、`qemu-macos.cfg` 和 `qemu-windows.cfg`。

## 文档

- [内存管理文档](doc/memory/01-overview.md)
- [内存管理设计笔记](doc/notes/memory/03-layout.md)
- [线程与调度器设计](doc/notes/thread/0-overview.md)
- [DMA 设计](doc/notes/dma/0-overview.md)
- [Bitmap](doc/notes/lib/1-bitmap.md) 与 [侵入式链表](doc/notes/lib/2-list.md)
- [驱动框架](doc/driver%20framework.md)
- [Multiboot2](doc/notes/01-multiboot2.md) 与 [x86_64 迁移记录](doc/notes/10-road_to_64.md)

## 近期演进

自 README 上次更新（2025-09-06，`0ad3a86`）以来，`master` 已累计 86 个提交，主要变化包括：

- **2025-09**：扩展 IPv4/TCP/IP 支持，加入 USB、UHCI、HID 和串口相关能力，并持续整理驱动模型；
- **2025-10**：重构驱动框架和构建系统，引入 configurator 生成统一的 C/Rust 构建配置；
- **2026-03 至 2026-04**：重写内存管理体系，迁移到 x86_64，并完善页表、锁和资源生命周期；
- **2026-06 至 2026-07**：重构 DMA、线程、上下文切换、调度、WaitQueue 和 Completion；
- **2026-08**：加入静态/动态 per-CPU 基础设施，并改进 Bitmap、侵入式链表和红黑树。

## License

本项目采用 [MIT License](LICENSE)。
