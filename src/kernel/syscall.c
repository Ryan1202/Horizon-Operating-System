#include <kernel/descriptor.h>
#include <kernel/thread.h>
#include <stdint.h>

uint32_t sys_getpid(void);

typedef void * syscall_t;
syscall_t syscall_table[][2] = {
	sys_getpid, 0
};

typedef unsigned long (*syscall0_func_t)(void);
typedef unsigned long (*syscall1_func_t)(unsigned long);
typedef unsigned long (*syscall2_func_t)(unsigned long, unsigned long);
typedef unsigned long (*syscall3_func_t)(unsigned long, unsigned long, unsigned long);

uint32_t sys_getpid(void)
{
	return get_current_thread()->pid;
}

void do_syscall(uint32_t func, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
	if (syscall_table[func])
	{
		switch ((uint32_t)syscall_table[func][1])
		{
		case 0:
			((syscall0_func_t)syscall_table[func][0])();
			break;
		case 1:
			((syscall1_func_t)syscall_table[func][0])(arg1);
			break;
		case 2:
			((syscall2_func_t)syscall_table[func][0])(arg1, arg2);
			break;
		case 3:
			((syscall3_func_t)syscall_table[func][0])(arg1, arg2, arg3);
			break;
		}
	}
}