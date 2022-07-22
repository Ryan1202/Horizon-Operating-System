/**
 * @file elf.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 读取elf文件
 * @version 0.1
 * @date 2021-10
 */
#include <fs/fs.h>
#include <kernel/memory.h>
#include <kernel/console.h>
#include <kernel/page.h>
#include <kernel/elf32.h>

/**
 * @brief 读取并解析elf文件
 * 
 * @param prog 
 * @return unsigned int* 
 */
unsigned int *elf_load(struct program_struct *prog)
{
	struct elf32_header header;
	fs_read(prog->inode, (uint8_t *)&header, 0, sizeof(struct elf32_header));
	
    if (memcmp(header.e_ident, "\177ELF\1\1\1", 7) || \
        header.e_type != ELF32_ET_EXEC || \
        header.e_machine != ELF32_EM_386 || \
        header.e_version != 1 || \
        header.e_phnum > 1024 || \
        header.e_phentsize != sizeof(struct elf32_program_header)) {
        printk("Error:unsupported executable file!\n");
		return NULL;
    }
	
	struct elf32_program_header pheader;
    Elf32_Off prog_header_off = header.e_phoff;
    Elf32_Half prog_header_size = header.e_phentsize;
	
	unsigned long i, j;
	prog->phnum = header.e_phnum;
	for (i = 0; i < header.e_phnum; i++)
	{
		memset(&pheader, 0, prog_header_size);
		fs_read(prog->inode, (uint8_t *)&pheader, prog_header_off, prog_header_size);
		if (pheader.p_type == PT_LOAD)
		{
			int j;
			struct prog_segment *progseg = kmalloc(sizeof(struct prog_segment));
			progseg->vaddr = pheader.p_vaddr;
			progseg->filesz = pheader.p_filesz;
			progseg->memsz = pheader.p_memsz;
			progseg->offset = pheader.p_offset;
			list_add_tail(&progseg->list, &prog->seg_head);
		}
		prog_header_off += prog_header_size;
	}
	return (unsigned int *)header.e_entry;
}