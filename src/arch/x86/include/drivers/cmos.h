#ifndef _CMOS_H
#define _CMOS_H

#include <kernel/func.h>

#define CMOS_REGS			0x70
#define CMOS_DATA			0x71

//CMOS寄存器
#define CMOS_SECONDS		0x00
#define CMOS_MINUTES		0x02
#define CMOS_HOURS			0x04
#define CMOS_WEEKDAY		0x06
#define CMOS_DAY_OF_MONTH	0x07
#define CMOS_MONTH			0x08
#define CMOS_YEAR			0x09
#define CMOS_STATUS_A		0x0a
#define CMOS_STATUS_B		0x0b
#define CMOS_FLOPPY_TYPE	0x10
#define CMOS_EQP			0x14
#define CMOS_CENTURY		0x32

#define CMOS_READ(reg) ({ \
	io_out8(CMOS_REGS, reg); \
	io_in8(CMOS_DATA); \
	})
	
#define BCD2BIN(bcd) ((bcd>>4)*10 + (bcd&0x0f))
	
struct time {
	int year, month, day;
	int hour, minute, second;
};

static inline void cmos_get_time(struct time *time)
{
	io_cli();
	do
	{
		time->year = CMOS_READ(CMOS_YEAR) + CMOS_READ(CMOS_CENTURY)*0x100;
		time->month = CMOS_READ(CMOS_MONTH);
		time->day = CMOS_READ(CMOS_DAY_OF_MONTH);
		time->hour = CMOS_READ(CMOS_HOURS);
		time->minute = CMOS_READ(CMOS_MINUTES);
		time->second = CMOS_READ(CMOS_SECONDS);		
	}while(time->second != CMOS_READ(CMOS_SECONDS));
	io_out8(CMOS_REGS, 0x00);
	io_sti();
}

#endif