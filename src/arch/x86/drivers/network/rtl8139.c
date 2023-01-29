#include <drivers/pci.h>
#include <kernel/console.h>
#include <kernel/descriptor.h>
#include <kernel/driver.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <kernel/network.h>
#include <kernel/page.h>
#include <kernel/spinlock.h>
#include <kernel/wait_queue.h>

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

static status_t rtl8139_enter(driver_t *drv_obj);
static status_t rtl8139_exit(driver_t *drv_obj);
static status_t rtl8139_open(struct _device_s *dev);
static status_t rtl8139_close(struct _device_s *dev);
static status_t rtl8139_write(struct _device_s *dev, uint8_t *buf, uint32_t offset, size_t size);

driver_func_t rtl8139_driver = {
	.driver_enter  = rtl8139_enter,
	.driver_exit   = rtl8139_exit,
	.driver_open   = rtl8139_open,
	.driver_close  = rtl8139_close,
	.driver_read   = NULL,
	.driver_write  = rtl8139_write,
	.driver_devctl = NULL,
};

struct read_request_s {
	uint8_t *buffer;
	uint8_t	 length;
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

	uint8_t *rx_buffer;
	uint32_t rx_buffer_phy;
} device_extension_t;

device_t		   *rtl8139;
device_extension_t *rtl8139_ext;

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

void rtl8139_handler(int irq) {
	wait_queue_t		  *wq;
	struct read_request_s *rq;
	uint16_t			   status = io_in16(rtl8139_ext->io_base + RTL8139_ISR);
	if (status & RTL8139_ISR_SERR) {
		printk("[RTL8139]SERR\n");
		io_out16(rtl8139_ext->io_base + RTL8139_ISR, RTL8139_ISR_SERR);
	}
	if (status & RTL8139_ISR_TimeOut) {
		printk("[RTL8139]TimeOut\n");
		io_out16(rtl8139_ext->io_base + RTL8139_ISR, RTL8139_ISR_TimeOut);
	}
	if (status & RTL8139_ISR_LenChg) {
		printk("[RTL8139]LenChg\n");
		io_out16(rtl8139_ext->io_base + RTL8139_ISR, RTL8139_ISR_LenChg);
	}
	if (status & RTL8139_ISR_FOVW) {
		printk("[RTL8139]FOVW\n");
		io_out16(rtl8139_ext->io_base + RTL8139_ISR, RTL8139_ISR_FOVW);
	}
	if (status & RTL8139_ISR_PUN_LinkChg) {
		printk("[RTL8139]PUN/LinkChg\n");
		io_out16(rtl8139_ext->io_base + RTL8139_ISR, RTL8139_ISR_PUN_LinkChg);
	}
	if (status & RTL8139_ISR_RXOVW) {
		printk("[RTL8139]RXOVW\n");
		io_out16(rtl8139_ext->io_base + RTL8139_ISR, RTL8139_ISR_RXOVW);
	}
	if (status & RTL8139_ISR_TER) {
		printk("[RTL8139]TER\n");
		io_out16(rtl8139_ext->io_base + RTL8139_ISR, RTL8139_ISR_TER);
	}
	if (status & RTL8139_ISR_TOK) {
		printk("[RTL8139]TOK\n");
		io_out16(rtl8139_ext->io_base + RTL8139_ISR, RTL8139_ISR_TOK);
	}
	if (status & RTL8139_ISR_RER) {
		printk("[RTL8139]RER\n");
		io_out16(rtl8139_ext->io_base + RTL8139_ISR, RTL8139_ISR_RER);
	}
	if (status & RTL8139_ISR_ROK) {
		printk("[RTL8139]ROK\n");
		io_out16(rtl8139_ext->io_base + RTL8139_ISR, RTL8139_ISR_ROK);

		wq = wait_queue_first(rtl8139_ext->wqm);
		rq = (struct read_request_s *)wq->private_data;
		memcpy(rq->buffer, rtl8139_ext->rx_buffer, rq->length);
	}
	return;
}

static status_t rtl8139_enter(driver_t *drv_obj) {
	int i;

	device_create(drv_obj, sizeof(device_extension_t), DEV_NAME, DEV_USB, &rtl8139);
	rtl8139_ext = rtl8139->device_extension;

	rtl8139_ext->device = pci_get_device_ById(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID);
	if (rtl8139_ext->device == NULL) {
		printk(COLOR_YELLOW "\n[RTL8139]Cannot find device!\n");
		device_delete(rtl8139);
		return NODEV;
	}

	pci_enable_bus_mastering(rtl8139_ext->device);
	rtl8139_ext->io_base = pci_device_get_io_addr(rtl8139_ext->device);
	rtl8139_ext->irq	 = rtl8139_ext->device->irqline;

	// 初始化RTL8139
	io_out8(rtl8139_ext->io_base + RTL8139_CONFIG1, 0x00);		// 通电
	io_out8(rtl8139_ext->io_base + RTL8139_CR, RTL8139_CR_RST); // 重置
	int f = 1;
	for (i = 0; i < 1000; i++) {
		if (!(io_in8(rtl8139_ext->io_base + RTL8139_CR) & 0x10)) {
			f = 0;
			break;
		}
	}
	if (f) { return FAILED; }

	// 判断型号
	uint32_t hwverid = io_in32(rtl8139_ext->io_base + RTL8139_TCR) & 0x7cc00000;
	for (i = 0; i < CHIP_INFO_NR; i++) {
		if (chips[i].hwverid == hwverid) {
			rtl8139_ext->info = &chips[i];
			break;
		}
	}

	// 读取MAC地址
	for (i = 0; i < 6; i++) {
		rtl8139_ext->mac_addr[i] = io_in8(rtl8139_ext->io_base + RTL8139_IDRN(i));
	}
	printk("[RTL8139]MAC Address:%02x:%02x:%02x:%02x:%02x:%02x\n", rtl8139_ext->mac_addr[0],
		   rtl8139_ext->mac_addr[1], rtl8139_ext->mac_addr[2], rtl8139_ext->mac_addr[3],
		   rtl8139_ext->mac_addr[4], rtl8139_ext->mac_addr[5]);

	io_out8(rtl8139_ext->io_base + RTL8139_9346CR, 0xc0); // Unlock

	// 分配接收缓冲区
	rtl8139_ext->rx_buffer = kmalloc(8192 + 16 + 1500);
	if (rtl8139_ext->rx_buffer == NULL) { return FAILED; }
	rtl8139_ext->rx_buffer_phy = vir2phy((uint32_t)rtl8139_ext->rx_buffer);
	io_out32(rtl8139_ext->io_base + RTL8139_RBSTART, rtl8139_ext->rx_buffer_phy);

	// 分配发送缓冲区
	for (i = 0; i < TX_DESC_NR; i++) {
		rtl8139_ext->tx_buffer[i]	  = kmalloc(ETH_MAX_FRAME_SIZE);
		rtl8139_ext->tx_buffer_phy[i] = vir2phy((uint32_t)rtl8139_ext->tx_buffer[i]);
	}

	io_out16(rtl8139_ext->io_base + RTL8139_MPC, 0);
	io_out16(rtl8139_ext->io_base + RTL8139_BMCR, 0x3100);
	io_out8(rtl8139_ext->io_base + RTL8139_MSR, 0x40);

	io_out32(rtl8139_ext->io_base + RTL8139_RCR,
			 RXFTH_NONE | RBLEN_64K | RTL8139_RCR_MXDMA(0x07) | RTL8139_RCR_AER | RTL8139_RCR_AR |
				 RTL8139_RCR_WRAP | RTL8139_RCR_AB | RTL8139_RCR_AM | RTL8139_RCR_APM | RTL8139_RCR_AAP);
	io_out32(rtl8139_ext->io_base + RTL8139_TCR, RTL8139_TCR_MXDMA(0x07) | RTL8139_TCR_TXRR(2));

	io_out32(rtl8139_ext->io_base + RTL8139_MARN(0), 0xffffffff);
	io_out32(rtl8139_ext->io_base + RTL8139_MARN(4), 0xffffffff);

	io_out8(rtl8139_ext->io_base + RTL8139_BMCR, 0x00); // Lock

	rtl8139_ext->tx_idx_w	 = 0;
	rtl8139_ext->tx_idx_done = 0;
	rtl8139_ext->wqm		 = create_wait_queue();
	rtl8139_ext->rqm		 = create_wait_queue();
	wait_queue_init(rtl8139_ext->wqm);
	wait_queue_init(rtl8139_ext->rqm);

	io_out8(rtl8139_ext->io_base + RTL8139_CR, RTL8139_CR_RE | RTL8139_CR_TE); // 允许接收和发送
	for (i = 0; i < TX_DESC_NR; i++) {
		io_out32(rtl8139_ext->io_base + RTL8139_TSADN(i), rtl8139_ext->tx_buffer_phy[i]);
		io_in32(rtl8139_ext->io_base + RTL8139_TSADN(i));
	}
	rtl8139_ext->tx_flags = ((256) << 11) & 0x3f0000;

	put_irq_handler(rtl8139_ext->irq, (irq_handler_t)rtl8139_handler);
	irq_enable(rtl8139_ext->irq);

	return SUCCUESS;
}

static status_t rtl8139_open(struct _device_s *dev) {
	io_out32(rtl8139_ext->io_base + RTL8139_MPC, 0);
	io_out16(rtl8139_ext->io_base + RTL8139_MULINT, 0);

	// 启用中断
	io_out16(rtl8139_ext->io_base + RTL8139_IMR, IMR_ALL);

	return SUCCUESS;
}

static status_t rtl8139_read(struct _device_s *dev, uint8_t *buf, uint32_t offset, size_t size) {
	device_extension_t	  *rtl8139_ext = dev->device_extension;
	wait_queue_t		  *wq;
	struct read_request_s *rq;

	wq		   = wait_queue_add(rtl8139_ext->wqm, sizeof(struct read_request_s));
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
