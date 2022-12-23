#ifndef _BOOT_H
#define _BOOT_H

typedef struct aout_symbol_table
{
    unsigned long tabsize;
    unsigned long strsize;
    unsigned long addr;
    unsigned long reserved;
} aout_symbol_table_t;

typedef struct elf_section_header_table
{
    unsigned long num;
    unsigned long size;
    unsigned long addr;
    unsigned long shndx;
} elf_section_header_table_t;

typedef struct multiboot_info
{
	unsigned int flags;
	unsigned int mem_low;
	unsigned int mem_high;
	unsigned int boot_device;
	unsigned int cmdline;
	unsigned int mods_count;
	unsigned int mods_addr;
	union
	{
		aout_symbol_table_t aout_sym;
		elf_section_header_table_t elf_sec;
	}u;
	unsigned int mmap_length;
	unsigned int mmap_addr;
	
}multiboot_info_t;

#endif