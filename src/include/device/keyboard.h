#ifndef _KB_H
#define _KB_H

#include <kernel/fifo.h>

#define KEYBOARD_IRQ	1

struct keystatus
{
	int num_lock, caps_lock, scroll_lock;		//键盘锁
	int left_ctrl, right_ctrl;
	int left_shift, right_shift;
	int left_alt, right_alt;
};

static char scan_codes1[95] =
{
	0,
	0,		'1',	'2',	'3',	'4',	'5',	'6',	'7',	'8',	'9',	'0',	'-',	'=',	'\b',
	0,		'q',	'w',	'e',	'r',	't',	'y',	'u',	'i',	'o',	'p',	'[',	']',	'\n',
	0,		'a',	's',	'd',	'f',	'g',	'h',	'j',	'k',	'l',	';',	'\'',	'`',
	0,		'\\',	'z',	'x',	'c',	'v',	'b',	'n',	'm',	',',	'.',	'/',	0,
};
static char scan_codes1_shift[95] =
{
	0,
	0,		'!',	'@',	'#',	'$',	'%',	'^',	'&',	'*',	'(',	')',	'_',	'+',	'\b',
	0,		'Q',	'W',	'E',	'R',	'T',	'Y',	'U',	'I',	'O',	'P',	'{',	'}',	'\n',
	0,		'A',	'S',	'D',	'F',	'G',	'H',	'J',	'K',	'L',	':',	'\"',	'~',
	0,		'|',	'Z',	'X',	'C',	'V',	'B',	'N',	'M',	'<',	'>',	'?',	0,
};

static char scan_codes2[0x5d+1] =
{
	0,		0,		0,		0,		0,		0,		0,		0,		0,		0,		0,		0,		0,		0,		'`',	0,		0,		0,		0,
	0,		0,		'q',	'1',	0,		0,		0,		'z',	's',	'a',	'w',	'2',	0,		0,		'c',	'x',	'd',	'e',	'4',
	'3',	0,		0,		' ',	'v',	'f',	't',	'r',	'5',	0,		0,		'n',	'b',	'h',	'g',	'y',	'6',	0,		0,
	0,		'm',	'j',	'u',	'7',	'8',	0,		0,		',',	'k',	'i',	'o',	'0',	'9',	0,		0,		'.',	'/',	'l',
	';',	'p',	'-',	0,		0,		0,		'\'',	0,		'[',	'=',	0,		0,		0,		0,		0,		']',	0,		'\\'
};
static char scan_codes2_shift[0x5d+1] =
{
	0,		0,		0,		0,		0,		0,		0,		0,		0,		0,		0,		0,		0,		0,		'~',	0,		0,		0,		0,
	0,		0,		'Q',	'!',	0,		0,		0,		'Z',	'S',	'A',	'W',	'@',	0,		0,		'C',	'X',	'D',	'E',	'$',
	'$',	0,		0,		0,		'V',	'F',	'T',	'R',	'%',	0,		0,		'N',	'B',	'H',	'G',	'Y',	'^',	0,		0,
	0,		'M',	'J',	'U',	'&',	'*',	0,		0,		'<',	'K',	'I',	'O',	'P',	'(',	0,		0,		'>',	'?',	'L',
	':',	'P',	'_',	0,		0,		0,		'\"',	0,		'{',	'+',	0,		0,		0,		0,		0,		'}',	0,		'|'
};

extern struct fifo keyfifo;

void init_keyboard(int off);
void keyboard_handler(int irq);
char scancode_analysis(int keycode);
void keyboard_setleds(void);

#endif