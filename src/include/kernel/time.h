#ifndef TIME_H
#define TIME_H

typedef struct {
	unsigned int year;
	unsigned int month;
	unsigned int day;
} date;

typedef struct {
	unsigned int hour;
	unsigned int minute;
	unsigned int second;
} time;

#endif