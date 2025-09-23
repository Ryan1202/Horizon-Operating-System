#include "kernel/driver.h"
#include "string.h"
#include <driver/input/input_dm.h>
#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/memory.h>
#include <objects/object.h>

string_t input_object = STRING_INIT("Input");

DriverResult input_dm_load(DeviceManager *manager);

DeviceManagerOps input_dm_ops = {
	.dm_load   = input_dm_load,
	.dm_unload = NULL,
};
InputDeviceManager input_dm_ext;
DeviceManager	   input_dm = {
		 .type		   = DEVICE_TYPE_INPUT,
		 .ops		   = &input_dm_ops,
		 .private_data = &input_dm_ext,
};

DriverResult input_dm_load(DeviceManager *manager) {
	ObjectAttr attr = base_obj_sys_attr;
	create_object_directory(&device_object, input_object, attr);
	memset(input_dm_ext.device_count, 0, sizeof(input_dm_ext.device_count));

	input_dm_ext.key_events =
		kmalloc(sizeof(KeyEvent) * INPUT_EVENT_QUEUE_SIZE);
	input_dm_ext.key_event_w = 0;
	input_dm_ext.key_event_r = 0;
	input_dm_ext.pointer_events =
		kmalloc(sizeof(PointerEvent) * INPUT_EVENT_QUEUE_SIZE);
	input_dm_ext.pointer_event_w = 0;
	input_dm_ext.pointer_event_r = 0;
	return DRIVER_RESULT_OK;
}

DriverResult register_input_device(
	DeviceDriver *device_driver, Device *device, Bus *bus,
	InputDevice *input_device) {
	device->dm_ext		 = input_device;
	input_device->device = device;

	int		 id = input_dm_ext.device_count[input_device->type]++;
	string_t name;
	if (input_device->type == INPUT_TYPE_KEYBOARD) {
		string_new_with_number(&name, "Keyboard", 8, id);
	} else if (input_device->type == INPUT_TYPE_MOUSE) {
		string_new_with_number(&name, "Mouse", 5, id);
	} else {
		string_new_with_number(&name, "Input", 5, id);
	}
	ObjectAttr attr = device_object_attr;
	register_device(device_driver, &name, bus, device, &attr);

	return DRIVER_RESULT_OK;
}

KeyEvent *new_key_event() {
	if (input_dm_ext.key_event_w == input_dm_ext.key_event_r) {
		// 队列满，丢弃最旧的事件
		input_dm_ext.key_event_r =
			(input_dm_ext.key_event_r + 1) % INPUT_EVENT_QUEUE_SIZE;
	}
	KeyEvent *event = &input_dm_ext.key_events[input_dm_ext.key_event_w];
	input_dm_ext.key_event_w =
		(input_dm_ext.key_event_w + 1) % INPUT_EVENT_QUEUE_SIZE;
	return event;
}

PointerEvent *new_pointer_event() {
	if (input_dm_ext.pointer_event_w == input_dm_ext.pointer_event_r) {
		// 队列满，丢弃最旧的事件
		input_dm_ext.pointer_event_r =
			(input_dm_ext.pointer_event_r + 1) % INPUT_EVENT_QUEUE_SIZE;
	}
	PointerEvent *event =
		&input_dm_ext.pointer_events[input_dm_ext.pointer_event_w];
	input_dm_ext.pointer_event_w =
		(input_dm_ext.pointer_event_w + 1) % INPUT_EVENT_QUEUE_SIZE;
	return event;
}
