#ifndef _CPU_H
#define _CPU_H

#define DEF_PER_CPU(type, name) type name __attribute__((section(".percpu")));

#endif