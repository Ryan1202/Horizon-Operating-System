/*
 * file:		include/lib/stdarg.h
 * auther:		Jason Hu
 * time:		2019/6/2
 * copyright:	(C) 2018-2020 by Book OS developers. All rights reserved.
 */

#ifndef _LIB_STDDEF_H
#define _LIB_STDDEF_H

/*
 *这里是typedef类型的
 */

/* 32位操作系统 */
typedef unsigned int size_t;
typedef int ssize_t;

typedef unsigned int ino_t;

typedef unsigned int blksize_t;
typedef unsigned int blkcnt_t;

typedef unsigned int dma_addr_t;

/* 64位操作系统 */
/*
typedef unsigned long flags_t;
typedef unsigned long size_t;
typedef unsigned long register_t;
typedef unsigned long address_t;
 */

typedef unsigned char * buf8_t;     // 字节类型的缓冲区
typedef unsigned short * buf16_t;   // 字类型的缓冲区
typedef unsigned int * buf32_t;     // 双字类型的缓冲区

typedef short wchar_t;

/*
 *这里是define类型的
 */
#define WRITE_ONCE(var, val) \
        (*((volatile typeof(val) *)(&(var))) = (val))

/* 
这里的__built_expect()函数是gcc(version >= 2.96)的内建函数,提供给程序员使用的，目的是将"分支转移"的信息提供给编译器，这样编译器对代码进行优化，以减少指令跳转带来的性能下降。
__buildin_expect((x), 1)表示x的值为真的可能性更大.
__buildin_expect((x), 0)表示x的值为假的可能性更大. 
*/
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#include "types.h"



#endif  /*_LIB_STDDEF_H*/

