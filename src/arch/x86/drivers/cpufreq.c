/**
 * @file cpufreq.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief CPU频率检测
 * @version 0.1 Beta
 * @date 2021-07
 *
 * @copyright Copyright (c) 2022
 *
 */
#include <drivers/cpufreq.h>
#include <drivers/msr.h>
#include <drivers/pit.h>
#include <kernel/fifo.h>
#include <kernel/func.h>

unsigned int cpu_get_freq(void) {
	unsigned int  tsc = 0, tsc2 = 0;
	struct timer *timer;
	struct fifo	  fifo;
	char		  fifo_buf[4];
	fifo_init(&fifo, 4, (int *)fifo_buf);
	// timer = timer_alloc();
	// timer_init(timer, &fifo, 128);

	// timer_settime(timer, 100);
	while (fifo_status(&fifo) == 0)
		io_hlt();
	fifo_get(&fifo);
	// timer_settime(timer, 100);
	__asm__ __volatile__("rdtsc \n\t" : "=A"(tsc));
	while (fifo_status(&fifo) == 0)
		;
	fifo_get(&fifo);
	__asm__ __volatile__("rdtsc \n\t" : "=A"(tsc2));

	// timer_free(timer);

	return (tsc2 - tsc) / 1000 / 1000;
}