#ifndef CONSOLE_H
#define CONSOLE_H

#define CMD_MAX_LENGTH	256
#define CMD_FLAG_INPUT	0
#define CMD_FLAG_OUTPUT 1

#define COLOR_BLACK	  "<0>"
#define COLOR_BLUE	  "<1>"
#define COLOR_GREEN	  "<2>"
#define COLOR_AQUA	  "<3>"
#define COLOR_RED	  "<4>"
#define COLOR_PURPLE  "<5>"
#define COLOR_YELLOW  "<6>"
#define COLOR_WHITE	  "<7>"
#define COLOR_GRAY	  "<8>"
#define COLOR_LBLUE	  "<9>"
#define COLOR_LGREEN  "<a>"
#define COLOR_LAQUA	  "<b>"
#define COLOR_LRED	  "<c>"
#define COLOR_LPURPLE "<d>"
#define COLOR_LYELLOW "<e>"
#define COLOR_BWHITE  "<f>"

struct console {
	unsigned char *vram;
	unsigned char *font;
	int			   start_x, start_y;
	int			   cur_x, cur_y;
	int			   width, height;
	int			   color;
	int			   flag;
};

void init_console(void);
void console_start(void);
void console_set_cursor(int x, int y);
void console_input(char c);
void print_char(unsigned char c, unsigned int color);
int	 printk(const char *fmt, ...);
void print_hex(unsigned char *s, int length);

#endif