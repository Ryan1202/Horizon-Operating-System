#include <fs/fs.h>
#include <kernel/memory.h>
#include <kernel/console.h>
#include <kernel/page.h>
#include <kernel/elf32.h>

unsigned int *elf_load(struct program_struct *prog)
{
	struct elf32_header header;
	prog->inode->f_ops.seek(prog->inode, 0, 0);
	fs_read(prog->inode, &header, sizeof(struct elf32_header));
	
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
		prog->inode->f_ops.seek(prog->inode, 0, prog_header_off);
		fs_read(prog->inode, &pheader, prog_header_size);
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
	return header.e_entry;
}