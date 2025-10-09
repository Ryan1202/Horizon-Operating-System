#ifndef _INPUT_DM_H
#define _INPUT_DM_H

#include "kernel/spinlock.h"
#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/device_manager.h>
#include <stdint.h>

#define INPUT_EVENT_QUEUE_SIZE 256

#define INPUT_KEY_EVENT_MOUSE_BASE	  0
#define INPUT_KEY_EVENT_MODIFIER_BASE 8
#define INPUT_KEY_EVENT_KEYBOARD_BASE 16

#define INPUT_KEY_PAGE_KEYBOARD_KEYPAD 0x07
#define INPUT_KEY_PAGE_CONSUMER		   0x0c
#define INPUT_KEY_PAGE_GENERAL_DESKTOP 0x01

typedef enum {
	INPUT_TYPE_UNKNOWN,
	INPUT_TYPE_KEYBOARD,
	INPUT_TYPE_MOUSE,
	INPUT_TYPE_MAX,
} InputDeviceType;

typedef struct InputDevice {
	LogicalDevice  *device;
	InputDeviceType type;
} InputDevice;

typedef struct KeyEvent {
	uint16_t keycode;
	uint8_t	 pressed; // 1: pressed, 0: released
	uint8_t	 page;
} KeyEvent;

typedef struct PointerEvent {
	int16_t dx;
	int16_t dy;
	enum PointerEventType {
		POINTER_TYPE_MOVE,
		POINTER_TYPE_SCROLL,
		POINTER_TYPE_PRESSURE,
	} type;
} PointerEvent;

typedef struct InputDeviceManager {
	int		   new_device_num[INPUT_TYPE_MAX];
	int		   device_count[INPUT_TYPE_MAX];
	spinlock_t lock[INPUT_TYPE_MAX];

	int		  key_event_w, key_event_r;
	bool	  key_event_full;
	KeyEvent *key_events;

	int			  pointer_event_w, pointer_event_r;
	bool		  pointer_event_full;
	PointerEvent *pointer_events;
} InputDeviceManager;

DriverResult create_input_device(
	InputDevice **input_device, InputDeviceType type, DeviceOps *ops,
	PhysicalDevice *physical_device, DeviceDriver *device_driver);
DriverResult delete_input_device(InputDevice *input_device);

extern DeviceManager input_dm;

void new_key_event(uint16_t keycode, uint8_t pressed, uint8_t page);
void new_pointer_event(int16_t dx, int16_t dy, enum PointerEventType type);

#endif