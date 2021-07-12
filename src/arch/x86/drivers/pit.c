#include <drivers/pit.h>
#include <drivers/apic.h>
#include <kernel/descriptor.h>
#include <kernel/func.h>
#include <kernel/fifo.h>
#include <kernel/console.h>
#include <config.h>

struct timerctl timerctl;

void init_timer(void)
{
	int i;
	io_out8(PIT_CTRL, 0x34);
	io_out8(PIT_CNT0, 0x9c);
	io_out8(PIT_CNT0, 0x2e);

	timerctl.count = 0;
	timerctl.next = 0xffffffff;					//没有定时器
	timerctl.using = 0;
	for(i = 0;i <= MAX_TIMER; i++)
	{
		timerctl.timers0[i].flags = TIMER_FREE;	//释放所有计时器
	}
	
	put_irq_handler(PIT_IRQ, timer_handler);
	irq_enable(PIT_IRQ);
	return;
}

void timer_handler(int irq)
{
	int i;
	struct timer *timer;
	timerctl.count++;
	if (timerctl.next > timerctl.count) {
		return;
	}
	timer = timerctl.timers[0];
	for(i = 0;i <= timerctl.using; i++)
	{
		if (timer->timeout > timerctl.count) {
			break;
		}
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


struct timer *timer_alloc(void)
{
	int i;
	for (i = 0; i < MAX_TIMER; i++) {
		if (timerctl.timers0[i].flags == 0) {
			timerctl.timers0[i].flags = TIMER_UNUSED;
			return &timerctl.timers0[i];
		}
	}
	return 0;
}

void timer_free(struct timer *timer)
{
	timer->flags = 0;
	return;
}

void timer_init(struct timer *timer, struct fifo *fifo, int data)
{
	timer->fifo = fifo;
	timer->data = data;
	return;
}

void timer_settime(struct timer *timer, unsigned int timeout)
{
	int e;
	struct timer *t, *s;
	timer->timeout = timeout + timerctl.count;
	timer->flags = TIMER_USING;
	e = io_load_eflags();
	io_cli();
	if(timerctl.using == 1) {
		timerctl.timers[0] = timer;
		timer->next = 0;
		timerctl.next = timer->timeout;
		io_store_eflags(e);
		return;
	}
	t = timerctl.timers[0];
	if(timer->timeout <= t->timeout) {
		timerctl.timers[0] = timer;
		timer->next = t;
		timerctl.next = timer->timeout;
		io_store_eflags(e);
		return;
	}
	for (;;) {
		s = t;
		t = t->next;
		if(t == 0) {
			break;
		}
		if(timer->timeout <= t->timeout) {
			s->next = timer;
			timer->next = t;
			io_store_eflags(e);
			return;
		}
	}
	s->next = timer;
	timer->next = 0;
	io_store_eflags(e);
	return;
}

void delay(int time)
{
	struct timer *timer;
	struct fifo fifo;
	char buf[1];
	timer = timer_alloc();
	fifo_init(&fifo, 1, buf);
	timer_init(timer, &fifo, 0);
	timer_settime(timer, time);
	while(!fifo_status(&fifo));
	fifo_get(&fifo);
	timer_free(timer);
	return;
}
