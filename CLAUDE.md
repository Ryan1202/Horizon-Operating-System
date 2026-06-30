# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

```bash
make              # Build the kernel ELF (generates src/build/ via configurator)
make hd           # Create bootable disk image with GRUB
make writehd      # Copy disk/ files into the image
make run          # Build and launch in QEMU
make run_dbg      # Launch QEMU with -s -S (GDB stub on :1234)
make lib          # Build userspace libraries
make app          # Build userspace applications
make clean        # Clean kernel, boot, apps, libs
```

The build system uses a custom **configurator** tool (`tools/configurator/`) that scans every directory for `config.toml` and generates:
- `src/build/Makefile` — unified kernel build
- `src/build/.cargo/config.toml` — Rust compilation configuration
- `src/root.rs` — auto-generated crate root with module declarations
- `compile_commands.json` — for clangd/LSP

Output is `src/build/kernel.elf`, a 64-bit ELF linked at `0xFFFFFFFF80000000`.

## Architecture

### Boot flow

1. **boot.asm** (legacy BIOS, `org 0x7c00`) — loads loader from floppy sectors
2. **multiboot2** (`src/arch/x86/multiboot2/`) — Multiboot2 header, memory map parsing, temporary page mappings for accessing high memory from GRUB's 32-bit protected mode
3. **start.asm** (`src/arch/x86/kernel/start.asm`) — entry point `kernel_early_start` sets up stack in physical space, calls `page_early_setup()`, then jumps to `kernel_start` (virtual address with higher-half mapping)
4. **page_early_setup** (`src/arch/x86/kernel/page.c`) — reconfigures PML4/PDPT/PDT with 2MB huge pages for the first 512MB, sets up the kernel's higher-half mapping at entry 511 of PML4
5. **main()** (`src/kernel/main.c`) — full kernel initialization: memory, object tree, device managers, bus manager, driver registration, initcalls, console

### Memory layout (x86_64 virtual address space)

| Region | Base | Size | Purpose |
|--------|------|------|---------|
| KLINEAR | `0xFFFF_8800_0000_0000` | 64TB | Linear mapping of all physical memory |
| VMALLOC | `0xFFFF_D000_0000_0000` | 32TB | Virtual-mapped allocations |
| VMEMMAP | `0xFFFF_F800_0000_0000` | 4TB | `struct page` metadata array |
| KERNEL | `0xFFFF_FFFF_8000_0000` | 512MB | Kernel code/data/bss |

Defined in [src/kernel/memory/mod.rs](src/kernel/memory/mod.rs).

### Memory allocator stack (Rust, `src/kernel/memory/`)

- **Frame allocator** (`frame/`) — buddy system with per-zone management, supports order-0 through order-3. Uses `Anonymous` (general-purpose) and `AssignedFixed` (reserved physical range) frame tags.
- **Slub** (`slub/`) — SLUB-like slab allocator using `MemCache` with per-CPU slabs (`MemCacheNode`). Configurable object sizes from 8 bytes to 4KB.
- **Page allocator** (`page/`) — virtual page allocation with `PageAllocOptions` for specifying frame sources, cache types, and page table locking.
- **DMA** (`dma/`) — coherent allocations, streaming mappings (scatter-gather and single), DMA pools. Written in Rust with C FFI exports (`#[unsafe(export_name = "dma_*")]`). Uses `Device` + `Constraints` + `Domain` architecture where `IdentityDomain` is the default backend.
- **kmalloc/vmalloc** — C-style allocator wrappers that call into the Rust allocator stack.

### Driver framework (C, `src/kernel/driver/`)

Two-level device model:
- **PhysicalDevice** — represents a hardware device on a bus
- **LogicalDevice** — abstracts a function of a physical device (one physical device can expose multiple logical devices)
- **Driver** — top-level container holding multiple `DeviceDriver` instances
- **Bus** — bus controller abstraction (PCI, ISA, USB)

Key init sequence: `init_device_managers()` → `init_bus_manager()` → `register_driver(&core_driver)` → `platform_start_devices()` → `do_initcalls()` → `driver_start_all()`

### Hardware drivers (`src/arch/x86/drivers/`)

- **disk/ata/** — ATA/IDE disk driver with DMA support
- **usb/hcd/uhci/** — UHCI USB host controller (skeleton, packet scheduling)
- **network/rtl8139/** — RTL8139 NIC
- **sound/sb16/** — Sound Blaster 16
- **framebuffer/vesa_display.c** — VESA/VBE display
- **input/8042/** — PS/2 keyboard and mouse
- **interrupt/** — 8259A PIC and APIC
- **timer/pit.c** — PIT (Programmable Interval Timer)
- **bus/pci/** — PCI enumeration
- **bus/isa/** — ISA bus and ISA DMA controller

### Object system (`src/objects/`)

Kernel namespace organized as an object tree with path-based lookup (`open_object_by_path`). Objects track permissions, handles, and attributes. Devices are registered as objects under `\Device\` and `\Volumes\`.

### Linker script

[src/arch/x86/kernel.lds](src/arch/x86/kernel.lds) defines:
- `.multiboot2` and `.early_init` at physical address `0x100000`
- Higher-half relocations at `VIR_BASE = 0xFFFFFFFF80000000`
- `.initcall` section with sorted initcall levels (0, 1)
- `.exitcall` section for teardown hooks
- `.data..percpu` for per-CPU data

## Key conventions

- **Rust-C FFI**: Rust memory code exports `#[unsafe(export_name = "...")]` functions consumed by C code. The DMA subsystem follows a Rust-first design with C compatibility layer.
- **Initcalls**: Driver/module initialization uses `initcall_0` and `initcall_1` section attributes, executed by `do_initcalls()`.
- **Config system**: Each source subdirectory has a `config.toml` listing its C source files and Rust module entries. The configurator walks the tree and generates a unified build.
- **Error handling**: C `DriverResult` enum with `DRIVER_RESULT_PASS` macro for early-return propagation. Rust uses `MemoryError` enum with conversion from `FrameError` and `PageTableError`.
- **Locking**: Custom spinlock (`src/include/kernel/spinlock.h`), RW lock (`rwlock.c`), and Rust read-write spinlock in `src/lib/rust/spinlock.rs`.
- **x86_64 target**: Custom Rust target `x86_64-unknown-none` with `code-model=kernel`, static relocation, no SIMD, no red zone, `force-frame-pointers=yes`.
