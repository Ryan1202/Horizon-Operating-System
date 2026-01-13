#ifndef _TIME_DM_H
#define _TIME_DM_H

#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>

typedef enum TimeType {
	TIME_TYPE_UNIX_TIMESTAMP, // Unix时间戳
	TIME_TYPE_UTC,			  // UTC时间
	TIME_TYPE_LOCAL,		  // 本地时间
	TIME_TYPE_MAX,
} TimeType;

typedef struct TimeUTC {
	uint16_t year;
	uint8_t	 month;
	uint8_t	 day;
	uint8_t	 hour;
	uint8_t	 minute;
	uint8_t	 second;
} TimeFull;

typedef union Time {
	uint32_t timestamp;
	TimeFull time;
} Time;

struct TimeDevice;
typedef struct TimeOps {
	DriverResult (*get_time)(
		struct TimeDevice *device, TimeType type, Time *time);
	DriverResult (*set_time)(
		struct TimeDevice *device, TimeType type, Time *time);
} TimeOps;

typedef struct TimeDevice {
	LogicalDevice *device;
	TimeType	   type;
	TimeOps		  *ops;
} TimeDevice;

typedef struct TimeDeviceManager {
	TimeDevice *time_devices[TIME_TYPE_MAX];
} TimeDeviceManager;

extern DeviceManager time_dm;

DriverResult create_time_device(
	TimeDevice **time_device, TimeOps *time_ops, DeviceOps *ops,
	PhysicalDevice *physical_device, DeviceDriver *device_driver);
DriverResult delete_time_device(TimeDevice *time_device);

DriverResult get_current_time(TimeType type, Time *time);

#endif