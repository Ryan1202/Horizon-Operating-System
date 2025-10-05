#ifndef _SERIAL_H
#define _SERIAL_H

#include "kernel/driver_interface.h"
#include <kernel/console.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/platform.h>
#include <stdint.h>

#define SERIAL_COM1_BASE 0x3F8
#define SERIAL_COM2_BASE 0x2F8
#define SERIAL_COM3_BASE 0x3E8
#define SERIAL_COM4_BASE 0x2E8

#define SERIAL_RATE_115200_DIVISION 1
#define SERIAL_RATE_57600_DIVISION	2
#define SERIAL_RATE_38400_DIVISION	3
#define SERIAL_RATE_19200_DIVISION	6
#define SERIAL_RATE_9600_DIVISION	12

#define SERIAL_RATE_DIVISION SERIAL_RATE_115200_DIVISION

#define SERIAL_COM1_IRQ 4
#define SERIAL_COM2_IRQ 3
#define SERIAL_COM3_IRQ 4
#define SERIAL_COM4_IRQ 3

#define SERIAL_UART_REG_DATA					 0
#define SERIAL_UART_REG_DIVISOR_LATCH_LOW		 0
#define SERIAL_UART_REG_INTERRUPT_ENABLE		 1
#define SERIAL_UART_REG_DIVISOR_LATCH_HIGH		 1
#define SERIAL_UART_REG_INTERRUPT_IDENTIFICATION 2
#define SERIAL_UART_REG_FIFO_CONTROL			 2
#define SERIAL_UART_REG_LINE_CONTROL			 3
#define SERIAL_UART_REG_MODEM_CONTROL			 4
#define SERIAL_UART_REG_LINE_STATUS				 5
#define SERIAL_UART_REG_MODEM_STATUS			 6
#define SERIAL_UART_REG_SCRATCH					 7

#define SERIAL_TEST_MAGIC_1 0x55
#define SERIAL_TEST_MAGIC_2 0xAA

typedef struct {
	LogicalDevice *device;
	uint16_t	   base_port;
	uint8_t		   irq_num;
	DeviceIrq	  *irq;

	ConsoleBackend console_backend;
} Serial;

void register_serial();

#endif