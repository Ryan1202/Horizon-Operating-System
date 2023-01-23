/**
 * @file fifo.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief FIFO缓冲区
 * 		参考了《三十天自制操作系统》的实现
 * @version 0.1
 * @date 2020-07
 */
#include <kernel/console.h>
#include <kernel/fifo.h>

/**
 * @brief 初始化FIFO缓冲区
 *
 * @param fifo FIFO结构
 * @param size 缓冲区大小
 * @param buf 缓冲区
 */
void fifo_init(struct fifo *fifo, int size, int *buf)
{
    fifo->size  = size;
    fifo->buf   = buf;
    fifo->free  = size;
    fifo->flags = 0;
    fifo->p     = 0;
    fifo->q     = 0;
    return;
}

/**
 * @brief 向FIFO缓冲区中写入数据
 *
 * @param fifo FIFO结构
 * @param data 要写入的数据
 * @return int 成功为0，失败为-1
 */
int fifo_put(struct fifo *fifo, int data)
{
    if (fifo->free == 0) {
        printk("fifo: put failed! (free=%d, size=%d)\n", fifo->free, fifo->size);
        fifo->flags |= 1;
        return -1;
    }
    fifo->buf[fifo->p] = data;
    fifo->p++;
    if (fifo->p == fifo->size) {
        fifo->p = 0;
    }
    fifo->free--;
    return 0;
}

/**
 * @brief 取出FIFO缓冲区中的数据
 *
 * @param fifo FIFO结构
 * @return int 成功为0，失败为-1
 */
int fifo_get(struct fifo *fifo)
{
    int data;
    if (fifo->free == fifo->size) {
        printk("fifo: get failed! (free=%d, size=%d)\n", fifo->free, fifo->size);
        return -1;
    }
    data = fifo->buf[fifo->q];
    fifo->q++;
    if (fifo->q == fifo->size) {
        fifo->q = 0;
    }
    fifo->free++;
    return data;
}

/**
 * @brief 获取FIFO大小
 *
 * @param fifo FIFO结构
 * @return int FIFO大小
 */
int fifo_status(struct fifo *fifo)
{
    return fifo->size - fifo->free;
}