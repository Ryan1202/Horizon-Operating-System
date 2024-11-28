OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

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
    .text ALIGN(4) : {
        *(.text)
        *(.init.text)
		*(.exit.text)
    } :text

    .initcall ALIGN(8) :
	{
		PROVIDE(__initcall_start = .);
		KEEP(*(.initcall_0.text))
        KEEP(*(.initcall_1.text))
		PROVIDE(__initcall_end = .);
	} :text

	.exitcall ALIGN(8) :
	{
		PROVIDE(__exitcall_start = .);
		KEEP(*(.exitcall_0.text))
		KEEP(*(.exitcall_1.text))
		PROVIDE(__exitcall_end = .);
	} :text

    /* data segment */
    .data ALIGN(4) : {
        *(.data)
    } :data

    /* bss segement */
    .bss ALIGN(4) : {
        *(.bss)
    } :data
    
    . = ALIGN(4);
    _end_of_file = .;  /* 标记文件结尾 */

    . = ALIGN(4);
    /DISCARD/ : {
        *(.note*)
        *(.comment)
    }
    .debug_aranges 0 (NOLOAD) : { *(.debug_aranges) }
    .debug_info 0 (NOLOAD) : { *(.debug_info) }
    .debug_abbrev 0 (NOLOAD) : { *(.debug_abbrev) }
    .debug_line 0 (NOLOAD) : { *(.debug_line) }
    .debug_str 0 (NOLOAD) : { *(.debug_str) }
    .debug_addr 0 (NOLOAD) : { *(.debug_addr) }
    .debug_line_str 0 (NOLOAD) : { *(.debug_line_str) }
    .debug_rnglists 0 (NOLOAD) : { *(.debug_rnglists) }
    .debug_str_offsets 0 (NOLOAD) : { *(.debug_str_offsets) }
    .debug_loclists 0 (NOLOAD) : { *(.debug_loclists) }
}