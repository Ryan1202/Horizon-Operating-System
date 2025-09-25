/**
 * @file console.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 控制文字的输出
 * @version 0.3
 * @date 2022-07-15
 */
#include <driver/framebuffer/fb.h>
#include <driver/framebuffer/fb_dm.h>
#include <kernel/console.h>
#include <kernel/font.h>
#include <kernel/list.h>
#include <kernel/sync.h>
#include <kernel/thread.h>
#include <math.h>
#include <stdio.h>

LIST_HEAD(console_backend_lh);

void console_register_backend(ConsoleBackend *backend, void *context) {
	if (backend == NULL || backend->put_string == NULL) return;
	backend->context = context;
	list_add_tail(&backend->list, &console_backend_lh);
	if (backend->init) { backend->init(context); }
}

/**
 * @brief 初始化控制台的配置
 *
 */
void init_console(void) {
}

void put_string(const char *string, int length) {
	ConsoleBackend *backend;
	list_for_each_owner (backend, &console_backend_lh, list) {
		backend->put_string(backend->context, string, length);
	}
}

/**
 * @brief 打印">"
 *
 */
void console_start(void) {
	put_string("\n>", 2);
}

/**
 * @brief 格式化输出
 *
 * @param fmt 格式字符串
 * @param ... 参数
 * @return int 字符串长度
 */
int printk(const char *fmt, ...) {
	int		i;
	char	buf[256];
	va_list arg;
	va_start(arg, fmt);
	i = vsprintf(buf, fmt, arg);
	va_end(arg);

	put_string(buf, i);
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
	print_string("\n          0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F  "
				 "0123456789ABCDEF\n");
	for (i = 0; i < DIV_ROUND_UP(length, 16); i++) {
		printk("%08X ", i * 16);
		for (j = 0; j < 16; j++) {
			if (j < length - i * 16) {
				printk("%02X ", s[i * 16 + j]);
			} else {
				print_string("   ");
			}
		}
		print_string(" ");
		for (j = 0; j < 16; j++) {
			if (j < length - i * 16) {
				if (s[i * 16 + j] < 32 || s[i * 16 + j] > 126) {
					print_string(".");
				} else {
					printk("%c", s[i * 16 + j]);
				}
			} else {
				print_string(" ");
			}
		}
		print_string("\n");
	}
}