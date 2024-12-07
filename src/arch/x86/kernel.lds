OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start) /* start is at boot/xxx/xxx.S */

PHDRS
{
    text PT_LOAD FLAGS(5); /* R_E: 可读 + 可执行 */
    data PT_LOAD FLAGS(6); /* R_W: 可读 + 可写 */
}

SECTIONS {
    /* Load the kernel at this address: "." means the current address */
    /*
    if use lower addr: . = 0x00100000;
    or not use high addr: . = 0x80100000;
    */
    . = 0x00100000;

    /* code segment */
    .text : {
        . = ALIGN(4);   /* 4 bytes align */
        *(.text)
        *(.init.text)
		*(.exit.text)
    } : text

    .initcall ALIGN(8) :
	{
		PROVIDE(__initcall_start = .);
		KEEP(*(.initcall_0.text))
        KEEP(*(.initcall_1.text))
		PROVIDE(__initcall_end = .);
	} : text

	.exitcall ALIGN(8) :
	{
		PROVIDE(__exitcall_start = .);
		KEEP(*(.exitcall_0.text))
		KEEP(*(.exitcall_1.text))
		PROVIDE(__exitcall_end = .);
	} : text

    /* data segment */
    .data : {
        . = ALIGN(4);   /* 4 bytes align */
        *(.data)
    } : data

    /* bss segement */
    .bss : {
        . = ALIGN(4);   /* 4 bytes align */
        *(.bss)
    } : data
}