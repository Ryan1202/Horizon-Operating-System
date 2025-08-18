#ifndef _NETWORK_H
#define _NETWORK_H

#include "kernel/list.h"
#include <kernel/driver.h>
#include <stdint.h>

typedef struct NetRxHandler {
	list_t list; // 链表节点
	void (*handler)(void *data);
	void *data; // 处理函数的私有数据
} NetRxHandler;

DriverResult network_softirq_register(NetRxHandler *handler);
void		 network_softirq_handler(void);

#endif