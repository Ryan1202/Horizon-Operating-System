#include "kernel/driver.h"
#include <driver/time_dm.h>
#include <drivers/cmos.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>

extern Driver core_driver;

DriverResult rtc_get_time(TimeDevice *time_device, TimeType type, Time *time);

DeviceDriverOps rtc_device_driver_ops = {

};
DeviceOps rtc_device_ops = {
	.init	 = NULL,
	.start	 = NULL,
	.stop	 = NULL,
	.destroy = NULL,
	.status	 = NULL,
};
TimeDeviceOps rtc_time_device_ops = {
	.get_time = rtc_get_time,
	.set_time = NULL,
};

DeviceDriver rtc_device_driver = {
	.name			   = STRING_INIT("RTC"),
	.type			   = DEVICE_TYPE_TIME,
	.priority		   = DRIVER_PRIORITY_BASIC,
	.ops			   = &rtc_device_driver_ops,
	.private_data_size = 0,
};
Device rtc_device = {
	.name			   = STRING_INIT("RTC"),
	.device_driver	   = &rtc_device_driver,
	.ops			   = &rtc_device_ops,
	.private_data_size = 0,
};
TimeDevice rtc_time_device = {
	.device = &rtc_device,
	.type	= TIME_TYPE_LOCAL,
	.ops	= &rtc_time_device_ops};

void register_cmos(void) {
	register_device_driver(&core_driver, &rtc_device_driver);
	register_time_device(&rtc_device_driver, &rtc_device, &rtc_time_device);
}

int rtc_guess_year(int year) {
	return 2000 + year;
}

bool rtc_is_updating(void) {
	return (CMOS_READ(CMOS_STATUS_A) & 0x80);
}

bool rtc_is_bcd(void) {
	return !(CMOS_READ(CMOS_STATUS_B) & 0x04);
}

DriverResult rtc_get_time(TimeDevice *time_device, TimeType type, Time *time) {
	while (rtc_is_updating())
		;
	int second = CMOS_READ(CMOS_SECONDS);
	int minute = CMOS_READ(CMOS_MINUTES);
	int hour   = CMOS_READ(CMOS_HOURS);
	int day	   = CMOS_READ(CMOS_DAY_OF_MONTH);
	int month  = CMOS_READ(CMOS_MONTH);
	int year   = CMOS_READ(CMOS_YEAR);

	if (rtc_is_bcd()) {
		second = BCD2BIN(second);
		minute = BCD2BIN(minute);
		hour   = BCD2BIN(hour);
		day	   = BCD2BIN(day);
		month  = BCD2BIN(month);
		year   = BCD2BIN(year);
	}

	time->time.year	  = rtc_guess_year(year);
	time->time.month  = month;
	time->time.day	  = day;
	time->time.hour	  = hour;
	time->time.minute = minute;
	time->time.second = second;

	return DRIVER_RESULT_OK;
}

DriverResult rtc_set_time(Device *device, TimeType type, Time *time) {
	return DRIVER_RESULT_OK;
}
