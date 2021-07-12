#ifndef _INITCALL_H
#define _INITCALL_H

typedef void (*initcall_t)(void);
typedef void (*exitcall_t)(void);

#define __init __attribute__ ((__section__ (".init.text")))
#define __exit __attribute__ ((__section__ (".exit.text")))

#define __define_initcall(level, fn, id) \
	static const initcall_t __initcall_##fn##id \
	__attribute__((__used__, __section__(".initcall_" level ".text"))) = fn

#define __define_exitcall(level, fn, id) \
	static const exitcall_t __exitcall_##fn##id \
	__attribute__((__used__, __section__(".exitcall_" level ".text"))) = fn

#define driver_initcall(fn)		__define_initcall("0", fn, 0)

#define driver_exitcall(fn)		__define_exitcall("0", fn, 0)

void do_initcalls(void);
void do_exitcalls(void);

#endif