#ifndef _PERCPU_H
#define _PERCPU_H

#define DEF_PER_CPU(type, name) \
	__attribute__((section(".data..percpu"))) type name

void init_percpu(void);

#endif