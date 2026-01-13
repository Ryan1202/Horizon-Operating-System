#ifndef _FAT_TIME_H
#define _FAT_TIME_H

#define FAT_DATE(year, month, day) (((year - 1980) << 9) | (month << 5) | day)
#define FAT_TIME(hour, minute, second) \
	((hour << 11) | (minute << 5) | (second >> 1))

#endif