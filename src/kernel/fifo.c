#include <kernel/fifo.h>
#include <kernel/console.h>

void fifo_init(struct fifo *fifo, int size, int *buf)
{
	fifo->size = size;
	fifo->buf = buf;
	fifo->free = size;
	fifo->flags = 0;
	fifo->p = 0;
	fifo->q = 0;
	return;
}

int fifo_put(struct fifo *fifo, int data)
{
	if(fifo->free == 0)
	{
		printk("fifo: put failed! (free=%d, size=%d)\n", fifo->free, fifo->size);
		fifo->flags |= 1;
		return -1;
	}
	fifo->buf[fifo->p] = data;
	fifo->p++;
	if(fifo->p == fifo->size)
	{
		fifo->p = 0;
	}
	fifo->free--;
	return 0;
}

int fifo_get(struct fifo *fifo)
{
	int data;
	if(fifo->free == fifo->size)
	{
		printk("fifo: get failed! (free=%d, size=%d)\n", fifo->free, fifo->size);
		return -1;
	}
	data = fifo->buf[fifo->q];
	fifo->q++;
	if(fifo->q == fifo->size)
	{
		fifo->q = 0;
	}
	fifo->free++;
	return data;
}

int fifo_status(struct fifo *fifo)
{
	return fifo->size - fifo->free;
}