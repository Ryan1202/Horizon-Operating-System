#include <driver/interrupt/interrupt_dm.h>
#include <driver/serial/serial_dm.h>
#include <drivers/serial.h>
#include <kernel/bus_driver.h>
#include <kernel/console.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_interface.h>
#include <kernel/memory.h>
#include <kernel/platform.h>
#include <stdint.h>
#include <string.h>

extern Driver core_driver;

DriverResult serial_init(void *device);
DriverResult serial_start(void *device);
DriverResult serial_self_test(SerialDevice *serial_device);
void		 serial_set_baud_rate(
			SerialDevice *serial_device, SerialBaudRate baud_rate);
void serial_set_recv_mode(
	SerialDevice *serial_device, SerialReceiveMode recv_mode);
void serial_set_recv_mode(
	SerialDevice *serial_device, SerialReceiveMode recv_mode);
void serial_console_backend_put_string(
	void *context, const char *string, int length);

DeviceOps serial_device_ops = {
	.init	 = serial_init,
	.start	 = serial_start,
	.stop	 = NULL,
	.destroy = NULL,
};
SerialOps serial_serial_ops = {
	.self_test	   = serial_self_test,
	.set_baud_rate = serial_set_baud_rate,
	.set_recv_mode = serial_set_recv_mode,
};

DeviceDriver serial_device_driver;

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

void serial_console_backend_put_string(
	void *context, const char *string, int length) {
	Serial *serial = context;
	if (serial->device->state != DEVICE_STATE_ACTIVE) return;

	for (int i = 0; i < length; i++) {
		// 等待直到可以发送数据
		while (!(
			io_in_byte(serial->base_port + SERIAL_UART_REG_LINE_STATUS) & 0x20))
			;
		io_out_byte(serial->base_port + SERIAL_UART_REG_DATA, string[i]);
	}
}

void serial_irq_handler(void *arg) {
	LogicalDevice *device		 = arg;
	SerialDevice  *serial_device = device->dm_ext;
	Serial		  *serial		 = device->private_data;
	uint16_t	   base_port	 = serial->base_port;
	uint8_t		   status =
		io_in_byte(base_port + SERIAL_UART_REG_INTERRUPT_IDENTIFICATION);

	// 判断中断类型
	if (!(status & 0x01)) {
		// 接收数据可用
		while (io_in_byte(base_port + SERIAL_UART_REG_LINE_STATUS) & 0x01) {
			uint8_t data = io_in_byte(base_port + SERIAL_UART_REG_DATA);
			// 处理接收到的数据（例如存入缓冲区）
			if (data == 127) data = '\b';
			if (data == '\r') data = '\n';
			if (serial_device->receive != NULL) {
				if (data == '\b') {
					serial_device->receive('\b');
					serial_device->receive(' ');
					serial_device->receive('\b');
				} else serial_device->receive(data);
			}
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
		return DRIVER_ERROR_OTHER;

	io_out_byte(base_port + SERIAL_UART_REG_MODEM_CONTROL, 0x0F);
	io_out_byte(base_port + SERIAL_UART_REG_INTERRUPT_ENABLE, val);

	return DRIVER_OK;
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

DriverResult serial_init(void *_device) {
	LogicalDevice *device	 = _device;
	Serial		  *serial	 = device->private_data;
	uint16_t	   base_port = serial->base_port;

	// 禁用中断
	io_out_byte(base_port + SERIAL_UART_REG_INTERRUPT_ENABLE, 0x00);
	// 8 位数据位，无奇偶校验，1 位停止位，禁用 DLAB
	io_out_byte(base_port + SERIAL_UART_REG_LINE_CONTROL, 0x03);
	// 启用 FIFO，清空接收和发送 FIFO，设置触发点为 8 字节
	io_out_byte(base_port + SERIAL_UART_REG_FIFO_CONTROL, 0x87);
	// 设置 RTS 和 DSR
	io_out_byte(base_port + SERIAL_UART_REG_MODEM_CONTROL, 0x0B);

	register_device_irq(
		&serial->irq, device->physical_device, serial->device, serial->irq_num,
		serial_irq_handler, IRQ_MODE_SHARED);

	serial->console_backend.init	   = NULL;
	serial->console_backend.put_string = serial_console_backend_put_string;

	return DRIVER_OK;
}

DriverResult serial_start(void *_device) {
	LogicalDevice *device	 = _device;
	Serial		  *serial	 = device->private_data;
	uint16_t	   base_port = serial->base_port;

	// 启用接收中断
	io_out_byte(base_port + SERIAL_UART_REG_INTERRUPT_ENABLE, 0x01);
	enable_device_irq(serial->irq);

	console_register_backend(&serial->console_backend, serial);

	return DRIVER_OK;
}

void register_serial() {
	register_device_driver(&core_driver, &serial_device_driver);

	DriverResult   result;
	SerialDevice  *serial_device;
	LogicalDevice *logical_device;
	for (int i = 0; i < sizeof(serial_ports) / sizeof(serial_ports[0]); i++) {
		if (serial_probe(serial_ports[i])) {
			result = create_serial_device(
				&serial_device, &serial_serial_ops, &serial_device_ops,
				platform_device, &serial_device_driver);
			if (result != DRIVER_OK) continue;

			logical_device	  = serial_device->device;
			Serial *serial	  = kmalloc(sizeof(Serial));
			serial->base_port = serial_ports[i];
			serial->irq_num	  = serial_irqs[i];
			serial->device	  = serial_device->device;

			logical_device->private_data = serial;
		}
	}
}