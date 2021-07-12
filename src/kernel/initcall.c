#include <kernel/initcall.h>

extern initcall_t __initcall_start[];
extern initcall_t __initcall_end[];
extern exitcall_t __exitcall_start[];
extern exitcall_t __exitcall_end[];

void do_initcalls(void)
{
	initcall_t *func = &(*__initcall_start);
	for (;func < &(*__initcall_end); func++)
	{
		(*func)();
	}
}

void do_exitcalls(void)
{
	initcall_t *func = &(*__exitcall_start);
	for (; func < &(*__exitcall_end); func++)
	{
		(*func)();
	}
}