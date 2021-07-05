#ifndef _PIT_H
#define _PIT_H

#define PIT_CTRL 0x43
#define PIT_CNT0 0x40

#define PIT_IRQ 2

#define MAX_TIMER 2048

#define TIMER_FREE 0
#define TIMER_UNUSED 1
#define TIMER_USING 2

struct timer
{
	struct timer *next;
	unsigned int timeout, flags;
	struct fifo *fifo;
	int data;
};

struct timerctl
{
	unsigned int count, next, using;
	struct timer *timers[MAX_TIMER];
	struct timer timers0[MAX_TIMER];
};

extern struct timerctl timerctl;

void init_timer(void);
void timer_handler(int irq);
struct timer *timer_alloc(void);
void timer_free(struct timer *timer);
void timer_init(struct timer *timer, struct fifo *fifo, int data);
void timer_settime(struct timer *timer, unsigned int timeout);
void delay(int time);

#endif