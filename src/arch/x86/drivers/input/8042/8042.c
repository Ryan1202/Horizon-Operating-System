/**
 * @file 8042.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 8042控制器的驱动
 * @version 0.1
 * @date 2021-06
 */
#include <drivers/8042.h>
#include <kernel/console.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_interface.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <kernel/memory.h>
#include <kernel/platform.h>
#include <objects/attr.h>
#include <stdint.h>

extern Driver core_driver;

bool usb_legacy_support_disabled = true;

DriverResult i8042_init(void *_device);
DriverResult i8042_start(void *_device);
DriverResult i8042_destroy(void *_device);

DeviceOps i8042_device_ops = {
	.init	 = i8042_init,
	.start	 = i8042_start,
	.stop	 = NULL,
	.destroy = i8042_destroy,
};

DeviceDriver	i8042_device_driver;
PhysicalDevice *i8042_device;

void i8042_irq_handler(void *arg) {
	do {
		i8042_read_data();
	} while (i8042_get_status(I8042_STAT_OUTBUF));
}

int i8042_get_status(uint8_t type) {
	int data = io_in_byte(I8042_PORT_STAT);
	return data & type;
}

void i8042_wait_ctr_send_ready(void) {
	for (;;) {
		if ((io_in_byte(I8042_PORT_STAT) & I8042_STAT_INBUF) ==
			0) { // 输入缓存区为空
			return;
		}
	}
}

void i8042_send_cmd(uint8_t command) {
	io_out_byte(I8042_PORT_CMD, command);
	i8042_wait_ctr_send_ready();
}

uint8_t i8042_read_data(void) {
	return io_in_byte(I8042_PORT_DATA);
}

void i8042_write_data(uint8_t data) {
	io_out_byte(I8042_PORT_DATA, data);
}

void i8042_disable_interrupt(int port) {
	I8042Device *i8042 = i8042_device->private_data;
	uint8_t		 cfg;
	disable_device_irq(i8042->irq[port]);
	i8042_send_cmd(I8042_CMD_READ);
	cfg = i8042_read_data();
	if (port == 0) {
		cfg &= ~I8042_CFG_INT1;
	} else if (port == 1) {
		cfg &= ~I8042_CFG_INT2;
	}
	i8042_send_cmd(I8042_CMD_WRITE);
	i8042_write_data(cfg);
}

void i8042_enable_interrupt(int port) {
	I8042Device *i8042 = i8042_device->private_data;
	uint8_t		 cfg;
	disable_device_irq(i8042->irq[port]);
	i8042_send_cmd(I8042_CMD_READ);
	cfg = i8042_read_data();
	if (port == 0) {
		cfg |= I8042_CFG_INT1;
	} else if (port == 1) {
		cfg |= I8042_CFG_INT2;
	}
	i8042_send_cmd(I8042_CMD_WRITE);
	i8042_write_data(cfg);
	enable_device_irq(i8042->irq[port]);
}

void i8042_clear_buffer(void) {
	while (i8042_get_status(I8042_STAT_OUTBUF)) {
		io_in_byte(I8042_PORT_DATA);
	}
}

DriverResult i8042_init(void *_device) {
	if (!usb_legacy_support_disabled) return DRIVER_ERROR_WAITING;

	PhysicalDevice *device = (PhysicalDevice *)_device;
	I8042Device	   *i8042  = device->private_data;

	// 1.禁用设备
	i8042_send_cmd(I8042_CMD_DISABLE_P1); // 禁用第一个PS/2端口
	i8042_send_cmd(I8042_CMD_DISABLE_P2); // 禁用第二个PS/2端口（如果有）

	// 2.配置控制器
	i8042_send_cmd(I8042_CMD_WRITE);
	i8042_write_data(
		I8042_CFG_TRANS1 | I8042_CFG_SYS_FLAG | I8042_CFG_INT2 |
		I8042_CFG_INT1);

	// 3.控制器自检
	i8042_send_cmd(I8042_CMD_TEST_CTL);
	if (i8042_read_data() != 0x55) { return DRIVER_ERROR_OTHER; }

	// 4.检测是否为双通道
	i8042_send_cmd(I8042_CMD_ENABLE_P2);
	i8042->is_dual_channel = (i8042_read_data() & I8042_CFG_CLK2) == 0;
	if (i8042->is_dual_channel) { // 存在第二个通道则禁用第二个通道
		i8042_send_cmd(I8042_CMD_DISABLE_P2);

		i8042_send_cmd(I8042_CMD_READ);
		uint8_t tmp = i8042_read_data();

		i8042_send_cmd(I8042_CMD_WRITE);
		i8042_write_data((tmp & ~I8042_CFG_INT2) | I8042_CFG_CLK2);
	}

	// 5.接口测试
	i8042_send_cmd(I8042_CMD_TEST_P1);
	if (i8042_read_data() != 0x00) {
		printk("[i8042]PS/2 Port 1 test failed!\n");
		i8042->is_p1_avail = false;
	} else {
		i8042->is_p1_avail = true;
		register_device_irq(
			&i8042->irq[0], i8042_device, NULL, ps2_irqs[0], i8042_irq_handler,
			IRQ_MODE_SHARED);
	}
	if (i8042->is_dual_channel) {
		i8042_send_cmd(I8042_CMD_TEST_P2);
		if (i8042_read_data() != 0x00) {
			printk("[i8042]PS/2 Port 2 test failed!\n");
			i8042->is_p2_avail = false;
		} else {
			i8042->is_p2_avail = true;
			register_device_irq(
				&i8042->irq[1], i8042_device, NULL, ps2_irqs[1],
				i8042_irq_handler, IRQ_MODE_SHARED);
		}
	}

	return DRIVER_OK;
}

DriverResult i8042_start(void *_device) {
	PhysicalDevice *physical_device = (PhysicalDevice *)_device;
	I8042Device	   *i8042			= physical_device->private_data;
	if (i8042->is_p1_avail) {
		// 6.启用端口
		i8042_send_cmd(I8042_CMD_ENABLE_P1);
		// 7.重置设备
		i8042_write_data(I8042_CMD_RESET_DEV);

		while (!i8042_get_status(I8042_STAT_OUTBUF))
			;
		if (i8042_read_data() != 0xfa) {
			printk("[i8042]PS/2 Port1 Device reset failed!\n");
			i8042->is_p1_avail = false;
			goto enable2;
		}
		while (!i8042_get_status(I8042_STAT_OUTBUF))
			;
		if (i8042_read_data() != 0xaa) {
			printk("[i8042]PS/2 Port1 Device self-test failed!\n");
			i8042->is_p1_avail = false;
			goto enable2;
		}

		if (i8042_get_status(I8042_STAT_OUTBUF)) {
			i8042->p1_dev_type = i8042_read_data();

			if (i8042->p1_dev_type == 0xfc) {
				printk("[i8042]PS/2 Port1 Device enable failed!\n");
				i8042->is_p1_avail = false;
				goto enable2;
			}
		} else { // AT键盘没有设备类型的响应
			i8042->p1_dev_type = 0xff;
		}
	}
enable2:
	if (i8042->is_p2_avail) {
		// 6.启用端口
		i8042_send_cmd(I8042_CMD_ENABLE_P2);

		// 7.重置设备
		i8042_send_cmd(I8042_CMD_SEND_TO_P2);
		i8042_write_data(I8042_CMD_RESET_DEV);

		while (!i8042_get_status(I8042_STAT_OUTBUF))
			;
		if (i8042_read_data() != 0xfa) {
			printk("[i8042]PS/2 Port2 Device reset failed!\n");
			i8042->is_p2_avail = false;
			goto next;
		}

		while (!i8042_get_status(I8042_STAT_OUTBUF))
			;
		if (i8042_read_data() != 0xaa) {
			printk("[i8042]PS/2 Port2 Device self-test failed!\n");
			i8042->is_p2_avail = false;
			goto next;
		}

		if (i8042_get_status(I8042_STAT_OUTBUF)) {
			i8042->p2_dev_type = i8042_read_data();
			if (i8042->p2_dev_type != 0xfa) {
				printk("[i8042]PS/2 Port2 Device enable failed!\n");
				i8042->is_p2_avail = false;
				goto next;
			}
		} else { // AT键盘没有设备类型的响应
			i8042->p2_dev_type = 0xff;
		}
	}
next:

	int available[2] = {i8042->is_p1_avail, i8042->is_p2_avail};
	int types[2]	 = {i8042->p1_dev_type, i8042->p2_dev_type};
	for (int i = 0; i < 2; i++) {
		if (!available[i]) {
			printk("[i8042]PS/2 Port %d is not available.\n", i + 1);
			continue;
		}

		printk("[i8042]PS/2 Port %d is available.Type: ", i + 1);
		switch (types[i]) {
		case 0x00:
			printk("Standard PS/2 Mouse");
			ps2_mouse_register(physical_device, i);
			break;
		case 0x03:
			printk("Mouse with scroll wheel");
			break;
		case 0xff: {
			printk("AT Keyboard");
			ps2_keyboard_register(physical_device, i);
			break;
		}
		default:
			printk("Unknown Device (0x%x)", types[i]);
			break;
		}
		printk("\n");

		i8042_enable_interrupt(i);
		enable_device_irq(i8042->irq[i]);
	}

	return DRIVER_OK;
}

DriverResult i8042_destroy(void *_device) {
	PhysicalDevice *device = (PhysicalDevice *)_device;
	kfree(device->private_data);
	return DRIVER_OK;
}

static __init void i8042_driver_entry(void) {
	register_device_driver(&core_driver, &i8042_device_driver);

	ObjectAttr attr = device_object_attr;
	create_physical_device(&i8042_device, platform_bus, &attr);
	register_physical_device(i8042_device, &i8042_device_ops);

	I8042Device *i8042		   = kzalloc(sizeof(I8042Device));
	i8042_device->private_data = i8042;
}

driver_initcall(i8042_driver_entry);