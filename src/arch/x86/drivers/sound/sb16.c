/**
 * @file sb16.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief Sound Blaster 16驱动
 * @version 0.1
 * @date 2022-07-22
 */
#include <drivers/8259a.h>
#include <drivers/apic.h>
#include <drivers/dma.h>
#include <drivers/pit.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/driver.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <kernel/spinlock.h>
#include <kernel/wait_queue.h>
#include <math.h>

#define SB16_DSP_MIXER		 0x224
#define SB16_DSP_MIXER_DATA	 0x225
#define SB16_DSP_RESET		 0x226
#define SB16_DSP_READ		 0x22a
#define SB16_DSP_WRITE		 0x22c
#define SB16_DSP_READ_STATUS 0x22e
#define SB16_DESP_IACK16	 0x22f

#define SB16_CMD_SET_TIME_CONSTANT		0x40
#define SB16_CMD_SET_OUTPUT_SAMPLE_RATE 0x41
#define SB16_CMD_TURN_SPEAKER_ON		0xd1
#define SB16_CMD_TURN_SPEAKER_OFF		0xd3
#define SB16_CMD_STOP_PLAY8				0xd0
#define SB16_CMD_RESUME_PLAY8			0xd4
#define SB16_CMD_STOP_PLAY16			0xd5
#define SB16_CMD_RESUME_PLAY16			0xd6
#define SB16_CMD_GET_DSP_VERSION		0xe1

#define SB16_CMD_SET_VOLUME 0x22
#define SB16_CMD_SET_IRQ	0x80

#define SB16_IRQ 0x05

static status_t sb16_enter(driver_t *drv_obj);
static status_t sb16_exit(driver_t *drv_obj);
status_t		sb16_open(device_t *device);
status_t		sb16_close(device_t *device);
status_t		sb16_write(device_t *dev, uint8_t *buf, uint32_t offset, size_t size);

#define DRV_NAME "Sound Blaster 16 Driver"
#define DEV_NAME "sb16"

#define DMA_MAX 4

device_t *sb16;
uint32_t  data_len[DMA_MAX];

driver_func_t sb16_driver = {.driver_enter	= sb16_enter,
							 .driver_exit	= sb16_exit,
							 .driver_open	= sb16_open,
							 .driver_close	= sb16_close,
							 .driver_read	= NULL,
							 .driver_write	= sb16_write,
							 .driver_devctl = NULL};

typedef struct {
	int					  major_ver, minor_ver;
	int					  index_r, index_w;
	spinlock_t			  lock;
	wait_queue_manager_t *wqm;
} device_extension_t;

void	 sb16_request(device_extension_t *devext);
void	 sb16_dma_config(uint8_t channel, uint32_t addr, uint32_t length);
void	 dsp_write(uint8_t value);
uint8_t	 dsp_read();
status_t dsp_reset(void);
void	 sb16_set_sample_rate(uint16_t samplerate);

void sb16_handler(int irq) {
	device_extension_t *devext = sb16->device_extension;
	dsp_write(SB16_CMD_STOP_PLAY16);
	io_in8(SB16_DSP_READ_STATUS);
	if (devext->major_ver >= 4) { io_in8(SB16_DESP_IACK16); }
	devext->index_r = (devext->index_r + 1) % DMA_MAX;

	wait_queue_wakeup(devext->wqm);

	if (devext->index_r != devext->index_w) { sb16_request(devext); }
	return;
}

static status_t sb16_enter(driver_t *drv_obj) {
	device_extension_t *devext;

	device_create(drv_obj, sizeof(device_extension_t), DEV_NAME, DEV_SOUND, &sb16);
	if (sb16 == NULL) {
		device_delete(sb16);
		return FAILED;
	}
	devext = sb16->device_extension;

	spinlock_init(&devext->lock);
	devext->wqm = create_wait_queue();
	wait_queue_init(devext->wqm);

	devext->index_r = devext->index_w = 0;
	memset((void *)0x800000, 0, 0x10000);
	memset((void *)0x810000, 0, 0x10000);
	memset((void *)0x820000, 0, 0x10000);
	memset((void *)0x830000, 0, 0x10000);

	// 复位DSP
	if (dsp_reset() != SUCCUESS) {
		device_delete(sb16);
		return FAILED;
	}

	// 获取DSP版本
	dsp_write(SB16_CMD_GET_DSP_VERSION);
	devext->major_ver = dsp_read();
	devext->minor_ver = dsp_read();
	printk("[SB16]DSP Version:%d.%d\n", devext->major_ver, devext->minor_ver);

	// 设置采样率
	sb16_set_sample_rate(44100);

	put_irq_handler(SB16_IRQ, (irq_handler_t)sb16_handler);
	irq_enable(SB16_IRQ);
	return SUCCUESS;
}

void sb16_dma_config(uint8_t channel, uint32_t addr, uint32_t length) {
	dma_disable(channel);
	dma_ff_reset(channel);
	dma_set_mode(channel, 0x58);
	dma_set_page(channel, addr >> 16);
	dma_set_addr(channel, addr);
	dma_set_count(channel, length);
	dma_enable(channel);
}

void dsp_write(uint8_t value) {
	while (io_in8(SB16_DSP_WRITE) & 0x80)
		;
	io_out8(SB16_DSP_WRITE, value);
}

uint8_t dsp_read() {
	while (!(io_in8(SB16_DSP_READ_STATUS) & 0x80))
		;
	return io_in8(SB16_DSP_READ);
}

void sb16_set_sample_rate(uint16_t samplerate) {
	dsp_write(SB16_CMD_SET_OUTPUT_SAMPLE_RATE);
	dsp_write((uint8_t)(samplerate >> 8));
	dsp_write((uint8_t)samplerate);
}

status_t dsp_reset(void) {
	io_out8(SB16_DSP_RESET, 1);
	delay(1);
	io_out8(SB16_DSP_RESET, 0);

	int data = dsp_read();
	if (data != 0xaa) {
		printk(COLOR_RED "[SB16]Reset Failed!\n");
		return FAILED;
	}
	return SUCCUESS;
}

status_t sb16_open(device_t *device) {
	device_extension_t *devext = device->device_extension;
	memset(data_len, 0, DMA_MAX * sizeof(uint32_t));
	return SUCCUESS;
}

status_t sb16_close(device_t *device) {
	return SUCCUESS;
}

void sb16_request(device_extension_t *devext) {
	int length = data_len[devext->index_r];
	if (devext->index_r == devext->index_w) { return; }

	sb16_dma_config(0x05, (uint32_t)(0x800000 + devext->index_r * 0x10000), data_len[devext->index_r]);
	uint16_t sample_count = length / sizeof(int16_t) / 2 - 1;

	dsp_write(0xb0);
	dsp_write(0x30);
	dsp_write((uint8_t)sample_count);
	dsp_write((uint8_t)(sample_count >> 8));
}

status_t sb16_write(device_t *dev, uint8_t *buf, uint32_t offset, size_t size) {
	if (size > 64 * 1024) { return FAILED; }
	device_extension_t *devext = dev->device_extension;
	int					i;
	while ((devext->index_w + 1) % DMA_MAX == devext->index_r) {
		wait_queue_add(devext->wqm, 0);
		thread_block(TASK_BLOCKED);
	}
	uint8_t *dma_mem = (uint8_t *)(0x800000 + devext->index_w * 0x10000);
	memcpy(dma_mem, buf, size);
	data_len[devext->index_w] = size;
	if (devext->index_r == devext->index_w) {
		devext->index_w = (devext->index_w + 1) % DMA_MAX;
		sb16_request(devext);
	} else {
		devext->index_w = (devext->index_w + 1) % DMA_MAX;
	}

	return SUCCUESS;
}

static status_t sb16_exit(driver_t *drv_obj) {
	device_t *devobj, *next;
	list_for_each_owner_safe (devobj, next, &drv_obj->device_list, list) {
		device_delete(devobj);
	}
	string_del(&drv_obj->name);
	return SUCCUESS;
}

static __init void sb16_driver_entry(void) {
	if (driver_create(sb16_driver, DRV_NAME) < 0) {
		printk(COLOR_RED "[driver] %s driver create failed!\n", __func__);
	}
}

driver_initcall(sb16_driver_entry);