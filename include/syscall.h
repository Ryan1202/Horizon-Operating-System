#ifndef SYSCALL_H
#define SYSCALL_H

#include "stddef.h"

int getpid(void);
int putchar(char c);
int puts(char *str);
int open(char *pathname, int flags);
int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, void *buf, size_t count);

#endif