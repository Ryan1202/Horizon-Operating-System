#include <syscall.h>

enum syscall_nr {
	sys_getpid = 0,
	sys_putchar,
	sys_puts
};

#define _syscall0(NUMBER) ({	\
	int ret;					\
	__asm__ __volatile__ (		\
	"int $0x80"					\
	: "=a" (ret)				\
	: "a" (NUMBER)				\
	: "memory"					\
	);							\
	ret;						\
})

#define _syscall1(NUMBER, ARG1) ({	\
	int ret;						\
	__asm__ __volatile__ (			\
	"int $0x80"						\
	: "=a" (ret)					\
	: "a" (NUMBER), "b"(ARG1)		\
	: "memory"						\
	);								\
	ret;							\
})

#define _syscall2(NUMBER, ARG1, ARG2) ({	\
	int ret;								\
	__asm__ __volatile__ (					\
	"int $0x80"								\
	: "=a" (ret)							\
	: "a" (NUMBER), "b"(ARG1), "c"(ARG2)	\
	: "memory"								\
	);										\
	ret;									\
})

#define _syscall3(NUMBER, ARG1, ARG2, ARG3) ({		\
	int ret;										\
	__asm__ __volatile__ (							\
	"int $0x80"										\
	: "=a" (ret)									\
	: "a" (NUMBER), "b"(ARG1), "c"(ARG2), "d"(ARG3)	\
	: "memory"										\
	);												\
	ret;											\
})

int getpid()
{
	return _syscall0(sys_getpid);
}

int putchar(char c)
{
	return _syscall1(sys_putchar, &c);
}

int puts(char *str)
{
	return _syscall1(sys_puts, str);
}