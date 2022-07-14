#include <kernel/descriptor.h>
#include <kernel/thread.h>
#include <stdint.h>

uint32_t sys_getpid(void);

typedef void * syscall_t;
syscall_t syscall_table[][2] = {
	sys_getpid , (void *)0,	// 此处存的是数字，只是做了类型转换
};

typedef unsigned long (*syscall0_func_t)(void);
typedef unsigned long (*syscall1_func_t)(unsigned long);
typedef unsigned long (*syscall2_func_t)(unsigned long, unsigned long);
typedef unsigned long (*syscall3_func_t)(unsigned long, unsigned long, unsigned long);

uint32_t sys_getpid(void)
{
	return get_current_thread()->pid;
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