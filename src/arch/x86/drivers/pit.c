/**
 * @file pit.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief
 * @version 1.0
 * @date 2022-07-31
 */
#include <drivers/8259a.h>
#include <drivers/apic.h>
#include <drivers/pit.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/fifo.h>
#include <kernel/func.h>

struct timerctl timerctl;

void init_timer(void) {
	int i;
	io_out8(PIT_CTRL, 0x34);
	io_out8(PIT_CNT0, 0x9c);
	io_out8(PIT_CNT0, 0x2e);

	timerctl.count = 0;
	timerctl.next  = 0xffffffff; // 没有定时器
	timerctl.using = 0;
	for (i = 0; i <= MAX_TIMER; i++) {
		timerctl.timers0[i].flags = TIMER_FREE; // 释放所有计时器
	}

	if (use_apic) {
		put_irq_handler(APIC_PIT_IRQ, timer_handler);
		irq_enable(APIC_PIT_IRQ);
	} else {
		put_irq_handler(PIC_PIT_IRQ, timer_handler);
		irq_enable(PIC_PIT_IRQ);
	}
	return;
}

void timer_handler(int irq) {
	int			  i;
	struct timer *timer;
	timerctl.count++;
	if (use_apic) {
		struct task_s *cur_thread = get_current_thread();
		cur_thread->elapsed_ticks++;

		if (cur_thread->ticks == 0) {
			schedule();
		} else {
			cur_thread->ticks--;
		}
	}
	if (timerctl.next > timerctl.count) { return; }
	timer = timerctl.timers[0];
	for (i = 0; i < timerctl.using; i++) {
		if (timer->timeout > timerctl.count) { break; }
		timer->flags = TIMER_UNUSED;
		fifo_put(timer->fifo, timer->data);
		timer = timer->next;
	}
	timerctl.using -= i;
	timerctl.timers[0] = timer;
	if (timerctl.using > 0) {
		timerctl.next = timerctl.timers[0]->timeout;
	} else {
		timerctl.next = 0xffffffff;
	}
	return;
}

struct timer *timer_alloc(void) {
	int i;
	for (i = 0; i < MAX_TIMER; i++) {
		if (timerctl.timers0[i].flags == 0) {
			timerctl.timers0[i].flags = TIMER_UNUSED;
			return &timerctl.timers0[i];
		}
	}
	return 0;
}

void timer_free(struct timer *timer) {
	if (timer->flags == TIMER_USING) { timerctl.using --; }
	timer->flags = TIMER_FREE;
	return;
}

void timer_init(struct timer *timer, struct fifo *fifo, int data) {
	timer->fifo = fifo;
	timer->data = data;
	return;
}

void timer_settime(struct timer *timer, unsigned int timeout) {
	int			  e;
	struct timer *t, *s;
	timer->timeout = timeout + timerctl.count;
	timer->flags   = TIMER_USING;
	e			   = io_load_eflags();
	io_cli();
	timerctl.using ++;
	if (timerctl.using == 1) {
		timerctl.timers[0] = timer;
		timer->next		   = 0;
		timerctl.next	   = timer->timeout;
		io_store_eflags(e);
		return;
	}
	t = timerctl.timers[0];
	if (timer->timeout <= t->timeout) {
		timerctl.timers[0] = timer;
		timer->next		   = t;
		timerctl.next	   = timer->timeout;
		io_store_eflags(e);
		return;
	}
	for (;;) {
		s = t;
		t = t->next;
		if (t == 0) { break; }
		if (timer->timeout <= t->timeout) {
			s->next		= timer;
			timer->next = t;
			io_store_eflags(e);
			return;
		}
	}
	s->next		= timer;
	timer->next = 0;
	io_store_eflags(e);
	return;
}

/**
 * @brief 等待
 *
 * @param time 时间（单位：10毫秒）
 */
void delay(int time) {
	struct timer *timer;
	struct fifo	  fifo;
	char		  buf[1];
	timer = timer_alloc();
	fifo_init(&fifo, 1, (int *)buf);
	timer_init(timer, &fifo, 0);
	timer_settime(timer, time);
	while (!fifo_status(&fifo))
		;
	fifo_get(&fifo);
	timer_free(timer);
	return;
}
