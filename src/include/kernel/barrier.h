#ifndef _BARRIER_H
#define _BARRIER_H

#ifdef ARCH_X86
#include <kernel/x86_barrier.h>
#else

#define memory_barrier()	  __asm__ __volatile__("" ::: "memory")
#define read_memory_barrier() memory_barrier()
#define wrie_memory_barrier() memory_barrier()

#endif

#define mb()  memory_barrier()
#define rmb() read_memory_barrier()
#define wmb() write_memory_barrier()

#endif