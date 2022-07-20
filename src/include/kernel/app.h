#ifndef APP_H
#define APP_H

#include <string.h>
#include <kernel/list.h>

struct prog_segment {
	unsigned int offset;	// 程序段在文件中的偏移
	unsigned int vaddr;		// 程序段的虚拟地址
	unsigned int filesz;	// 程序段在文件中的大小
	unsigned int memsz;		// 程序段在内存中的大小
	list_t list;
};

/**
 * @brief 程序的有关信息
 * 
 * 记录了一个程序(*.elf)的相关信息
 */
struct program_struct {
	struct index_node * inode;
	string_t name;		// 程序名
	string_t filename;	// 程序的源文件名
	string_t path;		// 源文件路径
	unsigned int phnum;
	list_t seg_head;
};

void run_app(char *path);

#endif
