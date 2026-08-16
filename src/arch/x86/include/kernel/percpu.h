#ifndef _PERCPU_H
#define _PERCPU_H

#include <stddef.h>

#define DEF_PER_CPU(type, name) \
	__attribute__((section(".data..percpu"))) type name

void percpu_init(size_t nr_cpus);

#endif
