#ifndef _INPUT_DM_H
#define _INPUT_DM_H

#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <stdint.h>

#define INPUT_EVENT_QUEUE_SIZE 256

#define INPUT_KEY_EVENT_MOUSE_BASE	  0
#define INPUT_KEY_EVENT_MODIFIER_BASE 8
#define INPUT_KEY_EVENT_KEYBOARD_BASE 16

typedef enum {
	INPUT_TYPE_UNKNOWN,
	INPUT_TYPE_KEYBOARD,
	INPUT_TYPE_MOUSE,
	INPUT_TYPE_MAX,
} InputDeviceType;

typedef struct InputDevice {
	Device		   *device;
	InputDeviceType type;
} InputDevice;

typedef struct KeyEvent {
	uint8_t keycode;
	uint8_t pressed; // 1: pressed, 0: released
	uint8_t page;
} KeyEvent;

typedef struct PointerEvent {
	int16_t dx;
	int16_t dy;
	enum {
		POINTER_TYPE_MOVE,
		POINTER_TYPE_SCROLL,
		POINTER_TYPE_PRESSURE,
	} type;
} PointerEvent;

typedef struct InputDeviceManager {
	int device_count[INPUT_TYPE_MAX];

	int		  key_event_w, key_event_r;
	KeyEvent *key_events;

	int			  pointer_event_w, pointer_event_r;
	PointerEvent *pointer_events;
} InputDeviceManager;

DriverResult register_input_device(
	DeviceDriver *device_driver, Device *device, Bus *bus,
	InputDevice *input_device);

extern DeviceManager input_dm;

KeyEvent	 *new_key_event();
PointerEvent *new_pointer_event();

#endif