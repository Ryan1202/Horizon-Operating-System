/**
 * @file syscall.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 系统调用相关
 * @version 0.1
 * @date 2022-05-01
 */
#include <fs/fs.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/thread.h>
#include <stdint.h>
#include <string.h>

int sys_getpid(void);
int sys_putchar(char c);
int sys_puts(char *str);
int sys_open(char *pathname, int flags);
int sys_close(int fd);
ssize_t sys_read(int fd, void *buf, size_t count);
ssize_t sys_write(int fd, void *buf, size_t count);

typedef void * syscall_t;
syscall_t syscall_table[][2] = {
	sys_getpid	,	(void *)0,	// 此处存的是数字，只是做了类型转换
	sys_putchar	,	(void *)1,
	sys_puts	,	(void *)1,
	sys_open	,	(void *)2,
	sys_close	,	(void *)1,
	sys_read	,	(void *)3,
	sys_write	,	(void *)3
};

typedef unsigned long (*syscall0_func_t)(void);
typedef unsigned long (*syscall1_func_t)(unsigned long);
typedef unsigned long (*syscall2_func_t)(unsigned long, unsigned long);
typedef unsigned long (*syscall3_func_t)(unsigned long, unsigned long, unsigned long);

int sys_getpid(void)
{
	return get_current_thread()->pid;
}

int sys_putchar(char c)
{
	printk("%c", c);
	return c;
}

int sys_puts(char *str)
{
	return printk("%s", str);
}

int sys_open(char *pathname, int flags)
{
	struct index_node *inode = fs_open(pathname);
	int fd = alloc_fd();
	fds[fd] = inode->fp;
	return fd;
}

int sys_close(int fd)
{
	int ret = fs_close(fds[fd]->inode);
	fds[fd] = NULL;
	return ret;
}

ssize_t sys_read(int fd, void *buf, size_t count)
{
	struct file *file = fds[fd];
	return fs_read(file->inode, buf, file->offset, count);
}

ssize_t sys_write(int fd, void *buf, size_t count)
{
	struct file *file = fds[fd];
	return fs_write(file->inode, buf, file->offset, count);
}

uint32_t do_syscall(uint32_t func, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
	uint32_t ret = 0;
	if (syscall_table[func])
	{
		switch ((uint32_t)syscall_table[func][1])
		{
		case 0:
			ret = ((syscall0_func_t)syscall_table[func][0])();
			break;
		case 1:
			ret = ((syscall1_func_t)syscall_table[func][0])(arg1);
			break;
		case 2:
			ret = ((syscall2_func_t)syscall_table[func][0])(arg1, arg2);
			break;
		case 3:
			ret = ((syscall3_func_t)syscall_table[func][0])(arg1, arg2, arg3);
			break;
		}
	}
	return ret;
}