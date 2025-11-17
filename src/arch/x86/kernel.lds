OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start) /* _start is at arch/x86/multiboot2/multiboot2.asm */

PHDRS
{
    text_phy PT_LOAD FLAGS(5); /* R_E: 可读 + 可执行 */
    text PT_LOAD FLAGS(5); /* R_E: 可读 + 可执行 */
    data PT_LOAD FLAGS(6); /* R_W: 可读 + 可写 */
}

VIR_BASE = 0xC0000000;
KERNEL_PHY_BASE = 0x00100000;

SECTIONS {
    . = KERNEL_PHY_BASE;
    _kernel_start_phy = .;
    _kernel_start_vir = VIR_BASE + KERNEL_PHY_BASE;

    /* multiboot2位置要尽量靠前 */
    .multiboot2 ALIGN(8) : {
        KEEP(*(.multiboot2))
    } : text_phy

    /* early_init需要在物理地址上进行 */
    .early_init : {
        KEEP(*(.early_init))
    } : text_phy
    
    . = . + VIR_BASE;

    /* code segment */
    .text : AT(ADDR(.text) - VIR_BASE) {
        . = ALIGN(4);   /* 4 bytes align */
        *(.text*)
        *(.init.text)
		*(.exit.text)
    } : text

    .initcall ALIGN(8) : AT(ADDR(.initcall) - VIR_BASE)
	{
		PROVIDE(__initcall_start = .);
		KEEP(*(.initcall_0.text))
        KEEP(*(.initcall_1.text))
		PROVIDE(__initcall_end = .);
	} : text

	.exitcall ALIGN(8) : AT(ADDR(.exitcall) - VIR_BASE)
	{
		PROVIDE(__exitcall_start = .);
		KEEP(*(.exitcall_0.text))
		KEEP(*(.exitcall_1.text))
		PROVIDE(__exitcall_end = .);
	} : text

    .data..percpu ALIGN(64) : AT(ADDR(.data..percpu) - VIR_BASE)
    {
        . = ALIGN(64);
        PROVIDE(__percpu_start = .);
        *(.data..percpu)
        . = ALIGN(64);
        PROVIDE(__percpu_end = .);
    } : data

    .rodata : AT(ADDR(.rodata) - VIR_BASE) {
        . = ALIGN(4);   /* 4 bytes align */
        *(.rodata*)
    } : data

    /* data segment */
    .data : AT(ADDR(.data) - VIR_BASE) {
        . = ALIGN(4);   /* 4 bytes align */
        *(.data)
    } : data

    /* bss segement */
    .bss : AT(ADDR(.bss) - VIR_BASE) {
        . = ALIGN(4);   /* 4 bytes align */
        *(.bss)
    } : data

    _kernel_end_vir = .;
    _kernel_end_phy = _kernel_end_vir - VIR_BASE;
}