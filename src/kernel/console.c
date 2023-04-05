/**
 * @file console.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 控制文字的输出
 * @version 0.3
 * @date 2022-07-15
 */
#include <drivers/video.h>
#include <kernel/console.h>
#include <kernel/font.h>
#include <kernel/sync.h>
#include <kernel/thread.h>
#include <math.h>
#include <stdio.h>

int			   command_length = 0;
char		   command[CMD_MAX_LENGTH];
struct console console;

/**
 * @brief 初始化控制台的配置
 *
 */
void init_console(void) {
	console.vram   = VideoInfo.vram;
	console.font   = font16;
	console.cur_x  = 0;
	console.cur_y  = 0;
	console.width  = VideoInfo.width / 10;
	console.height = VideoInfo.height / 16;
	console.color  = 0xc0c0c0;
	console.flag   = CMD_FLAG_OUTPUT;
}

/**
 * @brief 打印">"
 *
 */
void console_start(void) {
	printk("\n>");
	console.start_x = 1;
	console.start_y = console.cur_y + 1;
	console.flag	= CMD_FLAG_INPUT;
}

/**
 * @brief 设置光标位置
 *
 * @param x 光标的x坐标
 * @param y 光标的y坐标
 */
void console_set_cursor(int x, int y) {
	console.cur_x = x;
	console.cur_y = y;
}

/*
void console_input(char c)
{
	int i;
	char str[2];
	if (console.flag == CMD_FLAG_INPUT)
	{
		if (c != '\n')
		{
			if (command_length < CMD_MAX_LENGTH - 1)
			{
				command[command_length] = c;
				command_length++;
				str[0] = c;
				str[1] = '\0';
				printk(str);
			}
		}
		else
		{
			for (i = 0; i <= command_length; i++)
			{
				command[i] = 0;
			}
			command_length = 0;
			printk("\n");
			console_start();
			//运行程序 or 其他操作
		}
	}
}
*/

/**
 * @brief 打印一个字符
 *
 * @param c 字符
 * @param color 颜色
 */
void print_char(char c, unsigned int color) {
	int		 i, j, k;
	uint32_t _color = color;
	print_word(console.cur_x * 10 + 1, console.cur_y * 16, console.font + c * 16, _color);
	console.cur_x++;
	if (console.cur_x >= console.width) {
		console.cur_x = 0;
		console.cur_y++;
	}
	if (console.cur_y >= console.height) {
		console.cur_y = console.height - 1;
		console.cur_x = 0;
		for (i = 16; i < console.height * 16; i++) {
			for (j = 0; j < console.width * 10; j++) {
				for (k = 0; k < VideoInfo.BitsPerPixel / 8; k++) {
					console.vram[((i - 16) * VideoInfo.width + j) * (VideoInfo.BitsPerPixel / 8) + k] =
						console.vram[(i * VideoInfo.width + j) * (VideoInfo.BitsPerPixel / 8) + k];
				}
			}
		}
		draw_rect(0, console.height * 16 - 16, console.width * 10, 16, 0);
	}
}

/**
 * @brief 格式化输出
 *
 * @param fmt 格式字符串
 * @param ... 参数
 * @return int 字符串长度
 */
int printk(const char *fmt, ...) {
	int		i, color = console.color;
	char	buf[256];
	va_list arg;
	va_start(arg, fmt);
	i = vsprintf(buf, fmt, arg);
	va_end();

	char *p	  = buf, c;
	int	  len = i;
	while (len) {
		c = *p++;
		if (c == '<' && len == i) {
			if (*(p + 1) == '>') {
				switch (*p) {
				case '0':
					color = 0x000000;
					break;
				case '1':
					color = 0x0037da;
					break;
				case '2':
					color = 0x13a104;
					break;
				case '3':
					color = 0x3a96dd;
					break;
				case '4':
					color = 0xc50f1f;
					break;
				case '5':
					color = 0x881798;
					break;
				case '6':
					color = 0xffff00;
					break;
				case '7':
					color = 0xcccccc;
					break;
				case '8':
					color = 0x767676;
					break;
				case '9':
					color = 0x3878ff;
					break;
				case 'a':
					color = 0x16c60c;
					break;
				case 'b':
					color = 0x61d6d6;
					break;
				case 'c':
					color = 0xe74856;
					break;
				case 'd':
					color = 0xb4009e;
					break;
				case 'e':
					color = 0xf9f1a5;
					break;
				case 'f':
					color = 0xf2f2f2;
					break;

				default:
					break;
				}
				p += 2;

				len -= 3;
				c = *p++;
			}
		}
		switch (c) {
		case '\n':
			console.cur_y++;
			console.cur_x = 0;
			if (console.cur_y >= console.height) {
				print_char('\n', color);
				if (console.cur_y < 0) { console.cur_y = 0; }
				if (console.flag == CMD_FLAG_INPUT) { console.start_y = console.cur_y; }
			}
			break;
		case '\b':
			if (console.flag == CMD_FLAG_INPUT) {
				if (console.cur_x != console.start_x && console.cur_y != console.start_y) {
					console.cur_x--;
					draw_rect(console.cur_x * 10, console.cur_y * 16, 10, 16, 0);
				}
			}
			break;
		case '\t':
			if ((console.cur_x % 4) == 0) { console.cur_x += 4; }
			while (console.cur_x % 4) {
				console.cur_x++;
			}
			break;
		default:
			draw_rect(console.cur_x * 10, console.cur_y * 16, 10, 16, 0);
			print_char(c, color);
			break;
		}
		len--;
	}

	return i;
}

/**
 * @brief 将数据以十六进制形式输出
 *
 * @param s 数据内容
 * @param length 数据长度
 */
void print_hex(unsigned char *s, int length) {
	int i, j;
	if (length > 512) {
		printk("\nData is too long!\n");
		return;
	}
	printk("\n          0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F  0123456789ABCDEF\n");
	for (i = 0; i < DIV_ROUND_UP(length, 16); i++) {
		printk("%08X ", i * 16);
		for (j = 0; j < 16; j++) {
			if (j < length - i * 16) {
				printk("%02X ", s[i * 16 + j]);
			} else {
				printk("   ");
			}
		}
		printk(" ");
		for (j = 0; j < 16; j++) {
			if (j < length - i * 16) {
				if (s[i * 16 + j] < 32 || s[i * 16 + j] > 126) {
					printk(".");
				} else {
					printk("%c", s[i * 16 + j]);
				}
			} else {
				printk(" ");
			}
		}
		printk("\n");
	}
}