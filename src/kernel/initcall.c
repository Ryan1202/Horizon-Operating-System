/**
 * @file initcall.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 驱动的入口和出口
 * @version 0.1
 * @date 2021-06
 *
 */
#include <kernel/driver.h>
#include <kernel/initcall.h>

extern initcall_t __initcall_start[];
extern initcall_t __initcall_end[];
extern exitcall_t __exitcall_start[];
extern exitcall_t __exitcall_end[];

void do_initcalls(void) {
	init_dm();
	initcall_t *func = &(*__initcall_start);
	for (; func < &(*__initcall_end); func++) {
		(*func)();
	}
	// driver_inited();
}

void do_exitcalls(void) {
	initcall_t *func = &(*__exitcall_start);
	for (; func < &(*__exitcall_end); func++) {
		(*func)();
	}
}