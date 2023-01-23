#ifndef _FIFO_H
#define _FIFO_H

struct fifo {
    int *buf;
    int  p, q, size, free, flags;
};

void fifo_init(struct fifo *fifo, int size, int *buf);
int  fifo_put(struct fifo *fifo, int data);
int  fifo_get(struct fifo *fifo);
int  fifo_status(struct fifo *fifo);

#endif