#include <drivers/pci.h>
#include <kernel/console.h>
#include <kernel/driver.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <kernel/page.h>
#include <kernel/spinlock.h>
#include <kernel/wait_queue.h>
#include <network/eth.h>
#include <network/network.h>

#include <drivers/network/rtl8139.h>

#define RTL8139_VENDOR_ID 0x10ec
#define RTL8139_DEVICE_ID 0x8139

#define DRV_NAME "Horizon Basic Network Driver"
#define DEV_NAME "RTL8139"

#define CHIP_INFO_NR 10
#define TX_DESC_NR	 4

#define CHIP_HAS_LWAKE 0x01

#define IMR_ALL                                                                                        \
	RTL8139_IMR_SERR | RTL8139_IMR_TimeOut | RTL8139_IMR_LenChg | RTL8139_IMR_FOVW | RTL8139_IMR_TER | \
		RTL8139_IMR_TOK | RTL8139_IMR_RER | RTL8139_IMR_ROK
#define RXFTH_NONE RTL8139_RCR_RXFTH(0x07)
#define RBLEN_64K  RTL8139_RCR_RBLEN(0x03)

#define RTL8139_RECV_BUF_SIZE 8192 + 16 + 1500

static status_t rtl8139_enter(driver_t *drv_obj);
static status_t rtl8139_exit(driver_t *drv_obj);
static status_t rtl8139_open(struct _device_s *dev);
static status_t rtl8139_close(struct _device_s *dev);
static status_t rtl8139_write(struct _device_s *dev, uint8_t *buf, uint32_t offset, size_t size);
static status_t rtl8139_read(struct _device_s *dev, uint8_t *buf, uint32_t offset, size_t size);
static status_t rtl8139_ioctl(struct _device_s *dev, uint32_t func_num, uint32_t value);

driver_func_t rtl8139_driver = {
	.driver_enter  = rtl8139_enter,
	.driver_exit   = rtl8139_exit,
	.driver_open   = rtl8139_open,
	.driver_close  = rtl8139_close,
	.driver_read   = rtl8139_read,
	.driver_write  = rtl8139_write,
	.driver_devctl = rtl8139_ioctl,
};

struct read_request_s {
	uint8_t *buffer;
	uint32_t length;
};

typedef struct {
	struct pci_device *device;
	uint32_t		   io_base;
	uint32_t		   irq;

	uint8_t			  mac_addr[6];
	struct chip_info *info;

	int8_t	 tx_idx_w;	  // 写入到第几个缓冲区了
	int8_t	 tx_idx_done; // 处理到第几个缓冲区了
	uint32_t tx_flags;	  // 传输标志
	uint8_t *tx_buffer[TX_DESC_NR];
	uint32_t tx_buffer_phy[TX_DESC_NR];

	wait_queue_manager_t *wqm;
	wait_queue_manager_t *rqm;

	uint16_t rx_offset;
	uint8_t *rx_buffer;
	uint32_t rx_buffer_phy;
} device_extension_t;

struct chip_info {
	const char *name;
	uint32_t	hwverid;
	uint32_t	flag;
} chips[CHIP_INFO_NR] = {
	{"RTL8139", 0b110000000 << 22, 0},
	{"RTL8139A", 0b111000000 << 22, 0},
	{"RTL8139A-G", 0b111010000 << 22, 0},
	{"RTL8139B", 0b111100000 << 22, CHIP_HAS_LWAKE},
	{"RTL8130", 0b111110000 << 22, CHIP_HAS_LWAKE},
	{"RTL8139C", 0b111010000 << 22, CHIP_HAS_LWAKE},
	{"RTL8100", 0b111100010 << 22, CHIP_HAS_LWAKE},
	{"RTL8100B/8139D", 0b111010001 << 22, CHIP_HAS_LWAKE},
	{"RTL8101", 0b111010011 << 22, CHIP_HAS_LWAKE},
};

void rtl8139_handler(device_t *devobj, int irq) {
	device_extension_t	  *devext = (device_extension_t *)devobj->device_extension;
	wait_queue_t		  *wq;
	struct read_request_s *rq;
	uint16_t			   status = io_in16(devext->io_base + RTL8139_ISR);
	uint16_t			   info, length;
	int					   i;
	if (status & RTL8139_ISR_SERR) {
		printk("[RTL8139]SERR\n");
		io_out16(devext->io_base + RTL8139_ISR, RTL8139_ISR_SERR);
	}
	if (status & RTL8139_ISR_TimeOut) {
		printk("[RTL8139]TimeOut\n");
		io_out16(devext->io_base + RTL8139_ISR, RTL8139_ISR_TimeOut);
	}
	if (status & RTL8139_ISR_LenChg) {
		printk("[RTL8139]LenChg\n");
		io_out16(devext->io_base + RTL8139_ISR, RTL8139_ISR_LenChg);
	}
	if (status & RTL8139_ISR_FOVW) {
		printk("[RTL8139]FOVW\n");
		io_out16(devext->io_base + RTL8139_ISR, RTL8139_ISR_FOVW);
	}
	if (status & RTL8139_ISR_PUN_LinkChg) {
		printk("[RTL8139]PUN/LinkChg\n");
		io_out16(devext->io_base + RTL8139_ISR, RTL8139_ISR_PUN_LinkChg);
	}
	if (status & RTL8139_ISR_RXOVW) {
		printk("[RTL8139]RXOVW\n");
		io_out16(devext->io_base + RTL8139_ISR, RTL8139_ISR_RXOVW);
	}
	if (status & RTL8139_ISR_TER) {
		printk("[RTL8139]TER\n");
		io_out16(devext->io_base + RTL8139_ISR, RTL8139_ISR_TER);
	}
	if (status & RTL8139_ISR_TOK) {
		printk("[RTL8139]TOK\n");
		io_out16(devext->io_base + RTL8139_ISR, RTL8139_ISR_TOK);
		devext->tx_idx_done = (devext->tx_idx_done + 1) % TX_DESC_NR;
	}
	if (status & RTL8139_ISR_RER) {
		printk("[RTL8139]RER\n");
		io_out16(devext->io_base + RTL8139_ISR, RTL8139_ISR_RER);
	}
	if (status & RTL8139_ISR_ROK) {
		printk("[RTL8139]ROK\n");
		io_out16(devext->io_base + RTL8139_ISR, RTL8139_ISR_ROK);

		i	   = devext->rx_offset;
		info   = *(uint16_t *)(devext->rx_buffer + i);
		length = *(uint16_t *)(devext->rx_buffer + i + 2) - 4;
		i += 4;
		if (length >= 14 && (info & 1)) {
			uint8_t *tmpbuffer = kmalloc(length);
			if (i + length >= RTL8139_RECV_BUF_SIZE) {
				memcpy(tmpbuffer, devext->rx_buffer + i, RTL8139_RECV_BUF_SIZE - i);
				i = RTL8139_RECV_BUF_SIZE - i;
				memcpy(tmpbuffer, devext->rx_buffer + i, length - i);
			} else {
				memcpy(tmpbuffer, devext->rx_buffer + i, length);
			}
			((eth_handler_t)devobj->drv_obj->dm->private_data)(devobj->drv_obj->dm, tmpbuffer, length);
			kfree(tmpbuffer);
		}
		devext->rx_offset = (devext->rx_offset + length + 8 + 3) & ~3;
		devext->rx_offset %= RTL8139_RECV_BUF_SIZE;
		io_out16(devext->io_base + RTL8139_CAPR, devext->rx_offset - 0x10);
		wait_queue_wakeup(devext->wqm);
	}
	return;
}

static status_t rtl8139_enter(driver_t *drv_obj) {
	device_t		   *devobj;
	device_extension_t *devext;
	int					i;

	device_create(drv_obj, sizeof(device_extension_t), DEV_NAME, DEV_ETH_NET, &devobj);
	devext = devobj->device_extension;

	devext->device = pci_get_device_ById(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID);
	if (devext->device == NULL) {
		printk(COLOR_YELLOW "\n[RTL8139]Cannot find device!\n");
		device_delete(devobj);
		return NODEV;
	}

	pci_enable_bus_mastering(devext->device);
	devext->io_base = pci_device_get_io_addr(devext->device);
	devext->irq		= devext->device->irqline;

	// 初始化RTL8139
	io_out8(devext->io_base + RTL8139_CONFIG1, 0x00);	   // 通电
	io_out8(devext->io_base + RTL8139_CR, RTL8139_CR_RST); // 重置
	int f = 1;
	for (i = 0; i < 1000; i++) {
		if (!(io_in8(devext->io_base + RTL8139_CR) & 0x10)) {
			f = 0;
			break;
		}
	}
	if (f) { return FAILED; }

	// 判断型号
	uint32_t hwverid = io_in32(devext->io_base + RTL8139_TCR) & 0x7cc00000;
	for (i = 0; i < CHIP_INFO_NR; i++) {
		if (chips[i].hwverid == hwverid) {
			devext->info = &chips[i];
			break;
		}
	}

	// 读取MAC地址
	for (i = 0; i < 6; i++) {
		devext->mac_addr[i] = io_in8(devext->io_base + RTL8139_IDRN(i));
	}
	printk("[RTL8139]MAC Address:%02x:%02x:%02x:%02x:%02x:%02x\n", devext->mac_addr[0], devext->mac_addr[1],
		   devext->mac_addr[2], devext->mac_addr[3], devext->mac_addr[4], devext->mac_addr[5]);

	io_out8(devext->io_base + RTL8139_9346CR, 0xc0); // Unlock

	// 分配接收缓冲区
	devext->rx_offset = 0;
	devext->rx_buffer = kmalloc(RTL8139_RECV_BUF_SIZE);
	if (devext->rx_buffer == NULL) { return FAILED; }
	devext->rx_buffer_phy = vir2phy((uint32_t)devext->rx_buffer);
	io_out32(devext->io_base + RTL8139_RBSTART, devext->rx_buffer_phy);

	// 分配发送缓冲区
	for (i = 0; i < TX_DESC_NR; i++) {
		devext->tx_buffer[i]	 = kmalloc(ETH_MAX_FRAME_SIZE);
		devext->tx_buffer_phy[i] = vir2phy((uint32_t)devext->tx_buffer[i]);
	}

	io_out16(devext->io_base + RTL8139_MPC, 0);
	io_out16(devext->io_base + RTL8139_BMCR, 0x3100);
	io_out8(devext->io_base + RTL8139_MSR, 0x40);

	io_out32(devext->io_base + RTL8139_RCR,
			 RXFTH_NONE | RBLEN_64K | RTL8139_RCR_MXDMA(0x07) | RTL8139_RCR_AER | RTL8139_RCR_AR |
				 RTL8139_RCR_WRAP | RTL8139_RCR_AB | RTL8139_RCR_AM | RTL8139_RCR_APM | RTL8139_RCR_AAP);
	io_out32(devext->io_base + RTL8139_TCR, RTL8139_TCR_MXDMA(0x07) | RTL8139_TCR_TXRR(2));

	io_out32(devext->io_base + RTL8139_MARN(0), 0xffffffff);
	io_out32(devext->io_base + RTL8139_MARN(4), 0xffffffff);

	io_out8(devext->io_base + RTL8139_BMCR, 0x00); // Lock

	devext->tx_idx_w	= 0;
	devext->tx_idx_done = 0;
	devext->wqm			= create_wait_queue();
	devext->rqm			= create_wait_queue();
	wait_queue_init(devext->wqm);
	wait_queue_init(devext->rqm);

	io_out8(devext->io_base + RTL8139_CR, RTL8139_CR_RE | RTL8139_CR_TE); // 允许接收和发送
	for (i = 0; i < TX_DESC_NR; i++) {
		io_out32(devext->io_base + RTL8139_TSADN(i), devext->tx_buffer_phy[i]);
		io_in32(devext->io_base + RTL8139_TSADN(i));
	}
	devext->tx_flags = ((256) << 11) & 0x3f0000;

	device_register_irq(devobj, devext->irq, &rtl8139_handler);

	return SUCCUESS;
}

static status_t rtl8139_open(struct _device_s *dev) {
	device_extension_t *devext = (device_extension_t *)dev->device_extension;
	io_out32(devext->io_base + RTL8139_MPC, 0);
	io_out16(devext->io_base + RTL8139_MULINT, 0);

	// 启用中断
	io_out16(devext->io_base + RTL8139_IMR, IMR_ALL);

	return SUCCUESS;
}

static status_t rtl8139_read(struct _device_s *dev, uint8_t *buf, uint32_t offset, size_t size) {
	device_extension_t	  *devext = dev->device_extension;
	wait_queue_t		  *wq;
	struct read_request_s *rq;

	wq		   = wait_queue_add(devext->wqm, sizeof(struct read_request_s));
	rq		   = (struct read_request_s *)wq->private_data;
	rq->buffer = buf;
	rq->length = size;
	thread_block(TASK_BLOCKED);

	return SUCCUESS;
}

static status_t rtl8139_write(struct _device_s *dev, uint8_t *buf, uint32_t offset, size_t size) {
	device_extension_t *devext = dev->device_extension;

	if (size > ETH_MAX_FRAME_SIZE) { return FAILED; }
	if (devext->tx_idx_w == (devext->tx_idx_done + 1) % TX_DESC_NR) {
		wait_queue_add(devext->wqm, 0);
		thread_block(TASK_BLOCKED);
	}
	memcpy(devext->tx_buffer[devext->tx_idx_w], buf, size);
	io_out32(devext->io_base + RTL8139_TSDN(devext->tx_idx_w), size);
	io_in32(devext->io_base + RTL8139_TSDN(devext->tx_idx_w));
	devext->tx_idx_w = (devext->tx_idx_w + 1) % TX_DESC_NR;

	return SUCCUESS;
}

static status_t rtl8139_ioctl(struct _device_s *dev, uint32_t func_num, uint32_t value) {
	int i;

	device_extension_t *devext = dev->device_extension;

	switch (func_num) {
	case NET_FUNC_GET_MTU: {
		uint32_t *p = (uint32_t *)value;
		*p			= ETH_MTU;
		break;
	}
	case NET_FUNC_GET_MAC_ADDR: {
		uint8_t *addr = (uint8_t *)value;
		for (i = 0; i < 6; i++) {
			addr[i] = devext->mac_addr[i];
		}
		break;
	}
	case NET_FUNC_SET_MAC_ADDR: {
		uint8_t *addr = (uint8_t *)value;
		for (i = 0; i < 6; i++) {
			devext->mac_addr[i] = addr[i];
		}
		break;
	}
	}
	return SUCCUESS;
}

static status_t rtl8139_close(struct _device_s *dev) {
	device_extension_t *devext = dev->device_extension;
	io_out16(devext->io_base + RTL8139_IMR, 0);

	return SUCCUESS;
}

static status_t rtl8139_exit(driver_t *drv_obj) {
	device_t *devobj, *next;
	list_for_each_owner_safe (devobj, next, &drv_obj->device_list, list) {
		device_delete(devobj);
	}
	string_del(&drv_obj->name);
	return SUCCUESS;
}

static __init void rtl8139_driver_entry(void) {
	if (driver_create(rtl8139_driver, DRV_NAME) < 0) {
		printk(COLOR_RED "[driver] %s driver create failed!\n", __func__);
	}
}

driver_initcall(rtl8139_driver_entry);
