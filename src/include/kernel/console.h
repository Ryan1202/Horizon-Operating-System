#ifndef CONSOLE_H
#define CONSOLE_H

#define CMD_MAX_LENGTH	256
#define CMD_FLAG_INPUT	0
#define CMD_FLAG_OUTPUT 1

#define COLOR_BLACK	   "\033[30m"
#define COLOR_RED	   "\033[31m"
#define COLOR_GREEN	   "\033[32m"
#define COLOR_BROWN	   "\033[33m"
#define COLOR_BLUE	   "\033[34m"
#define COLOR_MAGENTA  "\033[35m"
#define COLOR_CYAN	   "\033[36m"
#define COLOR_WHITE	   "\033[37m"
#define COLOR_GRAY	   "\033[1;30m"
#define COLOR_BBLACK   "\033[1;30m"
#define COLOR_BRED	   "\033[1;31m"
#define COLOR_BGREEN   "\033[1;32m"
#define COLOR_BYELLOW  "\033[1;33m"
#define COLOR_BBLUE	   "\033[1;34m"
#define COLOR_BMAGENTA "\033[1;35m"
#define COLOR_BCYAN	   "\033[1;36m"
#define COLOR_BWHITE   "\033[1;37m"
#define COLOR_RESET	   "\033[m"

#define COLOR_BG_BLACK	  "\033[40m"
#define COLOR_BG_RED	  "\033[41m"
#define COLOR_BG_GREEN	  "\033[42m"
#define COLOR_BG_BROWN	  "\033[43m"
#define COLOR_BG_BLUE	  "\033[44m"
#define COLOR_BG_MAGENTA  "\033[45m"
#define COLOR_BG_CYAN	  "\033[46m"
#define COLOR_BG_WHITE	  "\033[47m"
#define COLOR_BG_GRAY	  "\033[1;40m"
#define COLOR_BG_BBLACK	  "\033[1;40m"
#define COLOR_BG_BRED	  "\033[1;41m"
#define COLOR_BG_BGREEN	  "\033[1;42m"
#define COLOR_BG_BYELLOW  "\033[1;43m"
#define COLOR_BG_BBLUE	  "\033[1;44m"
#define COLOR_BG_BMAGENTA "\033[1;45m"
#define COLOR_BG_BCYAN	  "\033[1;46m"
#define COLOR_BG_BWHITE	  "\033[1;47m"

#define print_string(str) put_string(str, strlen(str))

void init_console(void);
void console_start(void);
int	 printk(const char *fmt, ...);
void print_hex(unsigned char *s, int length);

// -----------new-----------

#include <kernel/list.h>

typedef struct ConsoleBackend {
	list_t list;
	void  *context;

	void (*init)(void *context);
	void (*put_string)(void *context, const char *string, int length);
} ConsoleBackend;

void console_register_backend(ConsoleBackend *backend, void *context);

#endif