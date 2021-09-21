#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

enum syscall_nr {
	sys_getpid
};

uint32_t getpid(void);

#endif