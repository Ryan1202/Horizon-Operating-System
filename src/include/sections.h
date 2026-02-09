#ifndef _SECTIONS_H
#define _SECTIONS_H

#define __multiboot2 __attribute__((section(".multiboot2")))
#define __early_init __attribute__((section(".early_init")))

#endif