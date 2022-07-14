#include <fs/fs.h>
#include <kernel/app.h>
#include <kernel/memory.h>
#include <kernel/console.h>
#include <kernel/elf32.h>
#include <kernel/process.h>

void run_app(char *path)
{
	struct program_struct *prog = kmalloc(sizeof(struct program_struct));
	prog->inode = fs_open(path);
	if (prog->inode == NULL)
	{
		printk("Cannot find file %s!", path);
	}
	string_init(&prog->name);
	string_init(&prog->filename);
	string_init(&prog->path);
	string_cpy(&prog->filename, &prog->inode->name);
	string_new(&prog->path, path, 255);
	
	char sign[4];
	fs_seek(prog->inode, 0, 0);
	fs_read(prog->inode, (uint8_t *)sign, 4);
	if (strncmp(sign, "\177ELF", 4) == 0)
	{
		string_cpy(&prog->name, &prog->filename);
		list_init(&prog->seg_head);
		unsigned int *entry = elf_load(prog);
		process_excute(entry, prog);
	}
	else
	{
		printk("Unsupport Executable File or Command %s!", prog->inode->name.text);
	}
	fs_close(prog->inode);
}