#include "kernel/console.h"
#include "kernel/driver.h"
#include "kernel/spinlock.h"
#include "string.h"
#include <driver/input/input_dm.h>
#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <kernel/memory.h>
#include <objects/object.h>
#include <stdint.h>

string_t input_object = STRING_INIT("Input");

DriverResult input_dm_load(DeviceManager *manager);
DriverResult input_dm_unload(DeviceManager *manager);

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
	for (int i = 0; i < INPUT_TYPE_MAX; i++) {
		input_dm_ext.new_device_num[i] = 0;
		input_dm_ext.device_count[i]   = 0;
		spinlock_init(&input_dm_ext.lock[i]);
	}

	input_dm_ext.key_events =
		kmalloc(sizeof(KeyEvent) * INPUT_EVENT_QUEUE_SIZE);
	input_dm_ext.key_event_w	= 0;
	input_dm_ext.key_event_r	= 0;
	input_dm_ext.key_event_full = false;
	input_dm_ext.pointer_events =
		kmalloc(sizeof(PointerEvent) * INPUT_EVENT_QUEUE_SIZE);
	input_dm_ext.pointer_event_w	= 0;
	input_dm_ext.pointer_event_r	= 0;
	input_dm_ext.pointer_event_full = false;
	return DRIVER_OK;
}

DriverResult input_dm_unload(DeviceManager *manager) {
	kfree(input_dm_ext.key_events);
	kfree(input_dm_ext.pointer_events);
	return DRIVER_OK;
}

DriverResult create_input_device(
	InputDevice **input_device, InputDeviceType type, DeviceOps *ops,
	PhysicalDevice *physical_device, DeviceDriver *device_driver) {
	DriverResult   result;
	LogicalDevice *logical_device = NULL;

	result = create_logical_device(
		&logical_device, physical_device, device_driver, ops,
		DEVICE_TYPE_INPUT);
	if (result != DRIVER_OK) return result;

	*input_device = kmalloc(sizeof(InputDevice));
	if (*input_device == NULL) {
		delete_logical_device(logical_device);
		return DRIVER_ERROR_OUT_OF_MEMORY;
	}
	InputDevice *in		   = *input_device;
	logical_device->dm_ext = in;
	in->device			   = logical_device;
	in->type			   = type;

	char	 name1[] = "Keyboard";
	char	 name2[] = "Mouse";
	char	 name3[] = "Input";
	char	*_name;
	uint8_t	 len;
	string_t name;
	uint8_t	 num;
	if (type == INPUT_TYPE_KEYBOARD) {
		_name = name1;
		len	  = sizeof(name1) - 1;
	} else if (type == INPUT_TYPE_MOUSE) {
		_name = name2;
		len	  = sizeof(name2) - 1;
	} else {
		_name = name3;
		len	  = sizeof(name3) - 1;
	}
	spin_lock(&input_dm_ext.lock[type]);
	num = input_dm_ext.new_device_num[type]++;
	input_dm_ext.device_count[type]++;
	spin_unlock(&input_dm_ext.lock[type]);
	string_new_with_number(&name, _name, len, num);

	Object *obj = create_object(&device_object, &name, device_object_attr);
	if (obj == NULL) {
		kfree(in);
		delete_logical_device(logical_device);
		return DRIVER_ERROR_OBJECT;
	}
	obj->value.device.kind	  = DEVICE_KIND_LOGICAL;
	obj->value.device.logical = logical_device;
	logical_device->object	  = obj;

	if (result != DRIVER_OK) return result;
	return DRIVER_OK;
}

DriverResult delete_input_device(InputDevice *input_device) {
	InputDeviceType type = input_device->type;
	spin_lock(&input_dm_ext.lock[type]);
	if (input_dm_ext.device_count[type] > 0) input_dm_ext.device_count[type]--;
	spin_unlock(&input_dm_ext.lock[type]);

	LogicalDevice *logical_device = input_device->device;

	list_del(&logical_device->dm_device_list);
	delete_logical_device(logical_device);
	int result = kfree(input_device);
	if (result < 0) return DRIVER_ERROR_MEMORY_FREE;
	return DRIVER_OK;
}

void new_key_event(uint16_t keycode, uint8_t pressed, uint8_t page) {
	if (input_dm_ext.key_event_full) {
		// 队列满，丢弃最旧的事件
		input_dm_ext.key_event_r =
			(input_dm_ext.key_event_r + 1) % INPUT_EVENT_QUEUE_SIZE;
		// 分配新的事件后仍然是满的，所以不更新full标志
	}
	KeyEvent *event = &input_dm_ext.key_events[input_dm_ext.key_event_w];
	event->keycode	= keycode;
	event->pressed	= pressed;
	event->page		= page;
	input_dm_ext.key_event_w =
		(input_dm_ext.key_event_w + 1) % INPUT_EVENT_QUEUE_SIZE;

	// 如果更新前w在前r在后，更新后相等说明队列满
	if (input_dm_ext.key_event_w == input_dm_ext.key_event_r) {
		input_dm_ext.key_event_full = true;
	}
	return;
}

void new_pointer_event(int16_t dx, int16_t dy, enum PointerEventType type) {
	if (input_dm_ext.pointer_event_full) {
		// 队列满，丢弃最旧的事件
		input_dm_ext.pointer_event_r =
			(input_dm_ext.pointer_event_r + 1) % INPUT_EVENT_QUEUE_SIZE;
		// 分配新的事件后仍然是满的，所以不更新full标志
	}
	PointerEvent *event =
		&input_dm_ext.pointer_events[input_dm_ext.pointer_event_w];
	event->dx	= dx;
	event->dy	= dy;
	event->type = type;
	input_dm_ext.pointer_event_w =
		(input_dm_ext.pointer_event_w + 1) % INPUT_EVENT_QUEUE_SIZE;

	if (input_dm_ext.pointer_event_w == input_dm_ext.pointer_event_r) {
		input_dm_ext.pointer_event_full = true;
	}
	return;
}
