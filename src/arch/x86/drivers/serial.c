#include "drivers/serial.h"
#include "driver/interrupt_dm.h"
#include "driver/serial/serial_dm.h"
#include "string.h"
#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_interface.h>
#include <kernel/memory.h>
#include <kernel/platform.h>
#include <stdint.h>

extern Driver core_driver;

DriverResult serial_init(Device *device);
DriverResult serial_start(Device *device);
DriverResult serial_self_test(SerialDevice *serial_device);
void		 serial_set_baud_rate(
			SerialDevice *serial_device, SerialBaudRate baud_rate);
void serial_set_recv_mode(
	SerialDevice *serial_device, SerialReceiveMode recv_mode);
void serial_set_recv_mode(
	SerialDevice *serial_device, SerialReceiveMode recv_mode);

DeviceDriverOps serial_device_driver_ops = {
	.device_driver_init	  = NULL,
	.device_driver_uninit = NULL,
};
DeviceOps serial_device_ops = {
	.init	 = serial_init,
	.start	 = serial_start,
	.stop	 = NULL,
	.destroy = NULL,
	.status	 = NULL,
};
SerialDeviceOps serial_serial_device_ops = {
	.self_test	   = serial_self_test,
	.set_baud_rate = serial_set_baud_rate,
	.set_recv_mode = serial_set_recv_mode,
};

DeviceDriver serial_device_driver = {
	.name	  = STRING_INIT("Serial"),
	.type	  = DEVICE_TYPE_SERIAL,
	.priority = DRIVER_PRIORITY_BASIC,
	.ops	  = &serial_device_driver_ops,
	.state	  = DRIVER_STATE_UNREGISTERED,
};

const uint16_t serial_ports[] = {
	SERIAL_COM1_BASE,
	SERIAL_COM2_BASE,
	SERIAL_COM3_BASE,
	SERIAL_COM4_BASE,
};
const int serial_irqs[] = {
	SERIAL_COM1_IRQ,
	SERIAL_COM2_IRQ,
	SERIAL_COM3_IRQ,
	SERIAL_COM4_IRQ,
};

string_t name = STRING_INIT("Serial");

void serial_irq_handler(Device *device) {
	SerialDevice *serial_device = device->dm_ext;
	Serial		 *serial		= device->private_data;
	uint16_t	  base_port		= serial->base_port;
	uint8_t		  status =
		io_in_byte(base_port + SERIAL_UART_REG_INTERRUPT_IDENTIFICATION);

	// 判断中断类型
	if (!(status & 0x01)) {
		// 接收数据可用
		while (io_in_byte(base_port + SERIAL_UART_REG_LINE_STATUS) & 0x01) {
			uint8_t data = io_in_byte(base_port + SERIAL_UART_REG_DATA);
			// 处理接收到的数据（例如存入缓冲区）
			if (serial_device->receive != NULL) {
				serial_device->receive(data);
			}
			io_out_byte(base_port + SERIAL_UART_REG_DATA, data); // 回显
		}
	}
	if (status & 0x02) {
		// 发送缓冲区空
		// 可以发送更多数据
	}
	if (status & 0x04) {
		// 接收线路状态改变
	}
	if (status & 0x08) {
		// 接收数据超时
	}
	if (status & 0x10) {
		// 接收FIFO达到触发点
	}
}

bool serial_probe(int base_port) {
	io_out_byte(base_port + SERIAL_UART_REG_SCRATCH, SERIAL_TEST_MAGIC_1);
	uint8_t val = io_in_byte(base_port + SERIAL_UART_REG_SCRATCH);
	if (val != SERIAL_TEST_MAGIC_1) return false;
	io_out_byte(base_port + SERIAL_UART_REG_SCRATCH, SERIAL_TEST_MAGIC_2);
	val = io_in_byte(base_port + SERIAL_UART_REG_SCRATCH);
	return val == SERIAL_TEST_MAGIC_2;
}

DriverResult serial_self_test(SerialDevice *serial_device) {
	Serial	*serial	   = serial_device->device->private_data;
	uint16_t base_port = serial->base_port;
	uint8_t	 val	   = io_in_byte(base_port + SERIAL_UART_REG_LINE_STATUS);

	// 禁用中断
	io_out_byte(base_port + SERIAL_UART_REG_INTERRUPT_ENABLE, 0x00);

	io_out_byte(base_port + SERIAL_UART_REG_MODEM_CONTROL, 0x1E);
	// 测试发送和接收
	io_out_byte(base_port + SERIAL_UART_REG_DATA, 0x5A);
	if (io_in_byte(base_port + SERIAL_UART_REG_DATA) != 0x5A)
		return DRIVER_RESULT_OTHER_ERROR;

	io_out_byte(base_port + SERIAL_UART_REG_MODEM_CONTROL, 0x0F);
	io_out_byte(base_port + SERIAL_UART_REG_INTERRUPT_ENABLE, val);

	return DRIVER_RESULT_OK;
}

void serial_set_baud_rate(
	SerialDevice *serial_device, SerialBaudRate baud_rate) {
	Serial	*serial	   = serial_device->device->private_data;
	uint16_t base_port = serial->base_port;

	uint16_t divisor = SERIAL_RATE_DIVISION;
	switch (baud_rate) {
	case SERIAL_BAUD_115200:
		divisor = SERIAL_RATE_115200_DIVISION;
		break;
	case SERIAL_BAUD_57600:
		divisor = SERIAL_RATE_57600_DIVISION;
		break;
	case SERIAL_BAUD_38400:
		divisor = SERIAL_RATE_38400_DIVISION;
		break;
	case SERIAL_BAUD_19200:
		divisor = SERIAL_RATE_19200_DIVISION;
		break;
	case SERIAL_BAUD_9600:
		divisor = SERIAL_RATE_9600_DIVISION;
		break;
	}

	// 启用 DLAB
	io_out_byte(base_port + SERIAL_UART_REG_LINE_CONTROL, 0x80);
	// 设置波特率
	io_out_byte(base_port + SERIAL_UART_REG_DIVISOR_LATCH_LOW, divisor & 0xff);
	io_out_byte(base_port + SERIAL_UART_REG_DIVISOR_LATCH_HIGH, divisor >> 8);
	// 禁用 DLAB
	io_out_byte(base_port + SERIAL_UART_REG_LINE_CONTROL, 0x03);
}

void serial_set_recv_mode(
	SerialDevice *serial_device, SerialReceiveMode recv_mode) {
	Serial	*serial	   = serial_device->device->private_data;
	uint16_t base_port = serial->base_port;
	if (recv_mode == SERIAL_RECV_MODE_HIGH_THROUGHPUT) {
		// 启用 FIFO，清空接收和发送 FIFO，设置触发点为 14 字节
		io_out_byte(base_port + SERIAL_UART_REG_FIFO_CONTROL, 0xC7);
	} else {
		// 启用 FIFO，清空接收和发送 FIFO，设置触发点为 1 字节
		io_out_byte(base_port + SERIAL_UART_REG_FIFO_CONTROL, 0x81);
	}
}

DriverResult serial_init(Device *device) {
	Serial	*serial	   = device->private_data;
	uint16_t base_port = serial->base_port;

	// 禁用中断
	io_out_byte(base_port + SERIAL_UART_REG_INTERRUPT_ENABLE, 0x00);
	// 8 位数据位，无奇偶校验，1 位停止位，禁用 DLAB
	io_out_byte(base_port + SERIAL_UART_REG_LINE_CONTROL, 0x03);
	// 启用 FIFO，清空接收和发送 FIFO，设置触发点为 8 字节
	io_out_byte(base_port + SERIAL_UART_REG_FIFO_CONTROL, 0x87);
	// 设置 RTS 和 DSR
	io_out_byte(base_port + SERIAL_UART_REG_MODEM_CONTROL, 0x0B);

	DeviceIrq *irq = kmalloc(sizeof(DeviceIrq));
	irq->device	   = device;
	irq->irq	   = serial->irq;
	irq->handler   = serial_irq_handler;
	device->irq	   = irq;
	register_device_irq(irq);

	return DRIVER_RESULT_OK;
}

DriverResult serial_start(Device *device) {
	Serial	*serial	   = device->private_data;
	uint16_t base_port = serial->base_port;

	// 启用接收中断
	io_out_byte(base_port + SERIAL_UART_REG_INTERRUPT_ENABLE, 0x01);
	interrupt_enable_irq(serial->irq);
	return DRIVER_RESULT_OK;
}

void register_serial() {
	serial_device_driver.ops = &serial_device_driver_ops;
	register_device_driver(&core_driver, &serial_device_driver);

	for (int i = 0; i < sizeof(serial_ports) / sizeof(serial_ports[0]); i++) {
		if (serial_probe(serial_ports[i])) {
			SerialDevice *serial_device = kmalloc(sizeof(SerialDevice));
			Device		 *device		= kmalloc(sizeof(Device));
			serial_device->device		= device;
			serial_device->ops			= &serial_serial_device_ops;
			device->name				= name;
			device->state				= DEVICE_STATE_UNREGISTERED;
			device->private_data_size	= sizeof(Serial);
			device->ops					= &serial_device_ops;
			device->max_child_device	= 0;

			register_serial_device(
				&serial_device_driver, device, &platform_bus, serial_device);

			Serial *serial	  = device->private_data;
			serial->base_port = serial_ports[i];
			serial->irq		  = serial_irqs[i];
			serial->device	  = device;
		}
	}
}