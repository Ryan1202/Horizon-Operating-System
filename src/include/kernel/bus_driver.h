#ifndef _BUS_DRIVER_H
#define _BUS_DRIVER_H

#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <objects/object.h>
#include <stdint.h>
#include <string.h>

struct BusDriver;

typedef enum BusType {
	BUS_TYPE_PLATFORM,
	BUS_TYPE_PCI,
	BUS_TYPE_ISA,
	BUS_TYPE_USB,
	BUS_TYPE_MAX,
} BusType;

typedef struct BusOps {
	DriverResult (*scan_bus)(struct BusDriver *bus_driver, struct Bus *bus);
	DriverResult (*probe_device)(struct BusDriver *bus_driver, struct Bus *bus);
} BusOps;

typedef struct BusDriver {
	list_t		bus_lh;
	string_t	name;
	BusType		bus_type;
	DriverState state;

	Object *object;

	uint16_t new_bus_num;
	uint16_t bus_count;
} BusDriver;

typedef struct Bus {
	list_t	   device_lh;
	list_t	   bus_list;
	list_t	   bus_check_list;
	list_t	   new_bus_list;
	BusDriver *bus_driver;

	struct Bus *primary_bus;

	Object *object;

	uint32_t bus_num;
	uint32_t subordinate_bus_num;

	int new_device_num;
	int device_count;

	BusOps *ops;
} Bus;

extern struct BusDriver *bus_drivers[BUS_TYPE_MAX];

DriverResult init_bus_manager();
DriverResult register_bus_driver(
	Driver *driver, BusType type, BusDriver *bus_driver, ObjectAttr *attr);
DriverResult unregister_bus_driver(BusDriver *bus_driver);
DriverResult create_bus(Bus **bus, BusDriver *bus_driver, BusOps *ops);
DriverResult delete_bus(Bus *bus);

#endif