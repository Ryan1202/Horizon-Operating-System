/**
 * @file 8042.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 8042控制器的驱动
 * @version 0.1
 * @date 2021-06
 */
#include <drivers/8042.h>
#include <kernel/console.h>
#include <kernel/driver.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <stdint.h>


static status_t i8042_enter(driver_t *drv_obj);
static status_t i8042_exit(driver_t *drv_obj);

#define DRV_NAME "General PS/2 Driver"
#define DEV_NAME "ps2controller"

typedef struct {
	bool is_dual_channel;
	bool is_p1_avail, is_p2_avail;

	uint8_t p1_dev_type, p2_dev_type;
} device_extension_t;

driver_func_t i8042_driver = {
	.driver_enter  = i8042_enter,
	.driver_exit   = i8042_exit,
	.driver_open   = NULL,
	.driver_close  = NULL,
	.driver_read   = NULL,
	.driver_write  = NULL,
	.driver_devctl = NULL,
};

int i8042_get_status(uint8_t type) {
	int data = io_in8(I8042_PORT_STAT);
	return data & type;
}

void i8042_wait_ctr_send_ready(void) {
	for (;;) {
		if ((io_in8(I8042_PORT_STAT) & I8042_STAT_INBUF) ==
			0) { // 输入缓存区为空
			return;
		}
	}
}

void i8042_send_cmd(int command) {
	io_out8(I8042_PORT_CMD, command);
	i8042_wait_ctr_send_ready();
}

int i8042_read_data(void) {
	return io_in8(I8042_PORT_DATA);
}

void i8042_write_data(int data) {
	io_out8(I8042_PORT_DATA, data);
}

static status_t i8042_enter(driver_t *drv_obj) {
	device_t		   *devobj;
	device_extension_t *devext;

	device_create(
		drv_obj, sizeof(device_extension_t), DEV_NAME, DEV_MANAGER, &devobj);
	devext = devobj->device_extension;

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
	if (i8042_read_data() != 0x55) {
		printk("[i8042]PS/2 Controller Self test failed!\n");
		device_delete(devobj);
		return FAILED;
	}

	// 4.检测是否为双通道
	i8042_send_cmd(I8042_CMD_ENABLE_P2);
	devext->is_dual_channel = (i8042_read_data() & I8042_CFG_CLK2) == 0;
	if (devext->is_dual_channel) { // 存在第二个通道则禁用第二个通道
		i8042_send_cmd(I8042_CMD_DISABLE_P2);

		i8042_send_cmd(I8042_CMD_READ);
		uint8_t tmp = i8042_read_data();

		i8042_send_cmd(I8042_CMD_WRITE);
		i8042_write_data(tmp & ~I8042_CFG_INT2 | I8042_CFG_CLK2);
	}

	// 5.接口测试
	i8042_send_cmd(I8042_CMD_TEST_P1);
	if (i8042_read_data() != 0x00) {
		printk("[i8042]PS/2 Port 1 test failed!\n");
		devext->is_p1_avail = false;
	} else {
		devext->is_p1_avail = true;
	}
	if (devext->is_dual_channel) {
		i8042_send_cmd(I8042_CMD_TEST_P1);
		if (i8042_read_data() != 0x00) {
			printk("[i8042]PS/2 Port 2 test failed!\n");
			devext->is_p2_avail = false;
		} else {
			devext->is_p2_avail = true;
		}
	}

	int i = 0;
	if (devext->is_p1_avail) {
		// 6.启用端口
		i8042_send_cmd(I8042_CMD_ENABLE_P1);
		// 7.重置设备
		i8042_write_data(I8042_CMD_RESET_DEV);

		for (i = 0; i < 2; i++) {
			if (i8042_read_data() == 0xfc) {
				printk("[i8042]PS/2 Port1 Device reset failed!\n");
			}
		}
		if (i8042_get_status(I8042_STAT_OUTBUF)) {
			devext->p1_dev_type = i8042_read_data();
		} else { // AT键盘没有设备类型的响应
			devext->p2_dev_type = 0xff;
			printk("[i8042]Found PS/2 device 1.Type: AT Keyboard\n");
		}
	}
	if (devext->is_p2_avail) {
		// 6.启用端口
		i8042_send_cmd(I8042_CMD_ENABLE_P2);

		i8042_send_cmd(I8042_CMD_READ);
		uint8_t tmp = i8042_read_data();

		i8042_send_cmd(I8042_CMD_WRITE);
		i8042_write_data(tmp | I8042_CFG_INT2);
		// 7.重置设备
		i8042_send_cmd(I8042_CMD_SEND_TO_P2);
		i8042_write_data(I8042_CMD_RESET_DEV);

		for (i = 0; i < 2; i++) {
			if (i8042_read_data() == 0xfc) {
				printk("[i8042]PS/2 Port1 Device reset failed!\n");
			}
		}
		if (i8042_get_status(I8042_STAT_OUTBUF)) {
			devext->p2_dev_type = i8042_read_data();
			if (devext->p2_dev_type == 0x00) {
				printk("[i8042]Found PS/2 device 2.Type: Mouse\n");
				i8042_send_cmd(I8042_CMD_SEND_TO_P2);
				i8042_write_data(0xf4);
				if (i8042_read_data() != 0xfa) {
					printk("[i8042]PS/2 Port2 Device: mouse enable failed!\n");
				}
			} else {
				printk(
					"[i8042]Found PS/2 device 2.Type: %#0X\n",
					devext->p2_dev_type);
			}
		} else { // AT键盘没有设备类型的响应
			devext->p2_dev_type = 0xff;
			printk("[i8042]Found PS/2 device 2.Type: AT Keyboard\n");
		}
	}
	return SUCCUESS;
}

static status_t i8042_exit(driver_t *drv_obj) {
	device_t *devobj, *next;
	// device_extension_t *ext;
	list_for_each_owner_safe (devobj, next, &drv_obj->device_list, list) {
		device_delete(devobj);
	}
	string_del(&drv_obj->name);
	return SUCCUESS;
}

static __init void i8042_driver_entry(void) {
	if (driver_create(i8042_driver, DRV_NAME) < 0) {
		printk(COLOR_RED "[driver] %s driver create failed!\n", __func__);
	}
}

driver_initcall(i8042_driver_entry);