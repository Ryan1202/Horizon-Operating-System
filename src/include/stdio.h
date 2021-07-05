#ifndef _STDIO_H_
#define _STDIO_H_

#include <types.h>
#include <stdint.h>
#include <stdarg.h>
#include <vsprintf.h>

#define STR_DEFAULT_LEN 256

/*conio*/
int write(char *str);

int getchar();
void putchar(char ch);
/*print*/
int printf(const char *fmt, ...);

#endif
