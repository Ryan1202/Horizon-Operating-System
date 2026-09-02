# ACPI Roadmap

## 当前进展

- `main` 已调用 ACPI 初始化：发现并校验 RSDP，遍历 RSDT/XSDT，管理 FADT/MADT，并从 FADT 加载 DSDT 的 AML。
- AML 已能建立命名空间和多类对象，并具备一部分表达式、方法、条件/循环、引用、`SystemMemory` / `SystemIO` Region 与 Mutex 执行能力。
- x86 已消费 MADT 的 Local APIC、I/O APIC 和 ISA Interrupt Source Override，并将 FADT/MADT 信息接入平台初始化；这仍不是完整的 ACPI OS/device/event/power 实现。

## Roadmap

### Tables

- [x] RSDP discovery and validation（x86 legacy scan）
- [x] RSDT / XSDT entry iteration
- [x] SDT signature / length / checksum validation（基础）
- [x] Table Manager（FADT / MADT / DSDT）
- [x] FADT parsing（boot capabilities / DSDT address）
- [x] DSDT mapping and AML loading
- [ ] FACS
- [ ] SSDT discovery and loading
- [ ] Generic table lookup / mapping
- [ ] BootInfo firmware handoff

### AML Runtime

- [x] AML parser foundation（NameString、PkgLength、常见对象与 Term）
- [x] Namespace tree
- [ ] Object Model（核心对象已建模，完整对象语义待补）
- [ ] Interpreter（已有基础 Executor，覆盖仍有限）
- [ ] Method Execution（基础调用、参数和返回值已接入）
- [ ] OperationRegion（SystemMemory / SystemIO 已支持）
- [ ] Field / BankField / IndexField（Field / BankField 部分支持，IndexField 与完整字段语义待补）
- [ ] Mutex / synchronization（基础锁已接入，超时与完整同步语义待补）
- [ ] Global Lock

### OS Services

- [ ] ACPI OS / architecture interface（x86 I/O 回调和基础映射已接入）
- [ ] Address-space handlers（PCI Config、EC、SMBus、GPIO、GenericSerialBus）
- [ ] AML table load / unload lifecycle
- [ ] ACPI timer / Stall / Sleep primitives
- [ ] Global Lock integration

### Device Model

- [ ] Namespace device enumeration（命名空间树已构建）
- [ ] `_HID / _CID / _UID`
- [ ] `_STA / _ADR`
- [ ] `_CRS / _PRS / _SRS`
- [ ] `_DEP / _DSD`
- [ ] ACPI Device abstraction
- [ ] Driver binding
- [ ] PCI Routing (`_PRT`)
- [ ] PCI Link Devices

### Events

- [ ] SCI
- [ ] Fixed Events
- [ ] GPE
- [ ] `_Lxx / _Exx`
- [ ] Notify / `_REG`
- [ ] Event → AML → Device dispatch

### Power & Thermal

- [ ] Reset / Reboot
- [ ] System Sleep (`_S0.._S5`)
- [ ] Sleep transition methods / Wakeup
- [ ] Device Power (`_PRx / _PSx`)
- [ ] Power Resources
- [ ] CPU Idle (`_CST`, LPI)
- [ ] CPU Performance / CPPC (`_CPC`)
- [ ] Thermal Zones (`_TMP / _CRT / _PSV / _ACx`)

### Embedded / Platform Control

- [ ] Generic Address Structure access
- [x] System I/O / System Memory region services
- [ ] PCI Config OperationRegion
- [ ] EC
- [ ] SMBus / GPIO / GenericSerialBus OperationRegion
- [ ] PCC / PCCT

### Architecture / Platform Description

- [x] MADT → interrupt-controller discovery（Local APIC / I/O APIC / ISA overrides）
- [ ] x2APIC
- [ ] MCFG → PCIe ECAM
- [ ] HPET → timer
- [ ] PPTT → CPU topology source
- [ ] SRAT → CPU / memory affinity
- [ ] SLIT → NUMA distance
- [ ] HMAT → memory performance topology
- [ ] DMAR → x86 IOMMU
- [ ] TPM2
- [ ] SPCR / DBG2

### Compatibility & Validation

- [ ] QEMU
- [ ] OVMF
- [ ] Real hardware
- [ ] ACPICA reference tests / AML fixtures
