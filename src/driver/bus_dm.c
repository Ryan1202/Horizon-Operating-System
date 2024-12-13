#include <driver/bus_dm.h>
#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/driver.h>

DriverResult bus_controller_start(DeviceManager *manager, Device *device);

DeviceManagerOps bus_controller_dm_ops = {
	.dm_load_hook	= NULL,
	.dm_unload_hook = NULL,
};

typedef struct BusControllerDeviceManager {
} BusControllerDeviceManager;

BusControllerDeviceManager bus_controller_dm_ext;

struct DeviceManager bus_controller_device_manager = {
	.type = DEVICE_TYPE_BUS_CONTROLLER,

	.ops = &bus_controller_dm_ops,

	.private_data = &bus_controller_dm_ext,
};

DriverResult register_bus_controller_device(
	DeviceDriver *device_driver, BusDriver *bus_driver, Device *device,
	BusControllerDevice *bus_controller_device) {

	device->device_driver			  = device_driver;
	bus_controller_device->device	  = device;
	bus_controller_device->bus_driver = bus_driver;

	DRV_RESULT_DELIVER_CALL(
		register_device, device_driver, device_driver->bus, device);
	return DRIVER_RESULT_OK;
}
