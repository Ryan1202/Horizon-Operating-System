#ifndef _X86_BARRIER_H
#define _X86_BARRIER_H

#if ARCH_X86 == 32
#define memory_barrier() \
	__asm__ __volatile__("lock; addl $0,0(%%esp)" ::: "memory")
#define read_memory_barrier()  memory_barrier()
#define write_memory_barrier() memory_barrier()
#else
#define memory_barrier()	   __asm__ __volatile__("mfence" ::: "memory")
#define read_memory_barrier()  __asm__ __volatile__("lfence" ::: "memory")
#define write_memory_barrier() __asm__ __volatile__("sfence" ::: "memory")
#endif

#endif