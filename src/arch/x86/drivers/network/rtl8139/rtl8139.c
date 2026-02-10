#include "rtl8139.h"
#include "kernel/platform.h"
#include <bits.h>
#include <driver/interrupt/interrupt_dm.h>
#include <driver/network/buffer.h>
#include <driver/network/ethernet/ethernet.h>
#include <driver/network/mii.h>
#include <driver/network/net_queue.h>
#include <driver/network/network.h>
#include <driver/network/network_dm.h>
#include <driver/timer/timer_dm.h>
#include <drivers/bus/pci/pci.h>
#include <drivers/network/rtl8139.h>
#include <kernel/barrier.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/driver_interface.h>
#include <kernel/initcall.h>
#include <kernel/memory.h>
#include <kernel/page.h>
#include <kernel/softirq.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <objects/object.h>
#include <objects/transfer.h>
#include <stdint.h>
#include <string.h>
#include <types.h>

DriverResult rtl8139_init(void *device);
DriverResult rtl8139_start(void *device);
DriverResult rtl8139_pci_probe(
	PciDevice *pci_device, PhysicalDevice *physical_device);
TransferResult rtl8139_send(NetworkDevice *device, void *buf, int length);

PciDriverOps rtl8139_pci_driver_ops = {
	.probe = rtl8139_pci_probe,
};
DeviceOps rtl8139_physical_device_ops = {
	.init	 = NULL,
	.start	 = NULL,
	.destroy = NULL,
	.stop	 = NULL,
};
DeviceOps rtl8139_logical_device_ops = {
	.init	 = rtl8139_init,
	.start	 = rtl8139_start,
	.destroy = NULL,
	.stop	 = NULL,
};
NetworkOps rtl8139_net_device_ops = {
	.send = rtl8139_send,
};

Driver		 rtl8139_driver;
DeviceDriver rtl8139_device_driver;
PciDriver	 rtl8139_pci_driver = {
	   .driver		  = &rtl8139_driver,
	   .device_driver = &rtl8139_device_driver,
	   .find_type	  = FIND_BY_VENDORID_DEVICEID,
	   .vendor_device = {RTL8139_VENDOR_ID, RTL8139_DEVICE_ID},
	   .ops			  = &rtl8139_pci_driver_ops,
};
NetworkDeviceCapabilities rtl8139_caps = {};

void rtl8139_handler(void *_device) {
	Rtl8139Device *rtl_device = _device;

	spin_lock(&rtl_device->lock);
	int status = io_in_word(rtl_device->io_base + REG_ISR);

	if (status == 0xffff) goto end;

	int _status = 0;
	if (status & IMR_TOK) {
		rtl_device->tx_done_idx = (rtl_device->tx_done_idx + 1) % TX_DESC_NR;
		_status |= IMR_TOK;
	}
	if (status & IMR_TER) {
		print_error("RTL8139", "TX Error\n");
		_status |= IMR_TER;
	}
	if (status & IMR_ROK) {
		pending_softirq();
		_status |= IMR_ROK;
	}
	if (status & IMR_RER) {
		print_error("RTL8139", "RX Error\n");
		_status |= IMR_RER;
	}
	if (status & IMR_RXOVW) {
		print_error("RTL8139", "RX Overflow\n");
		_status |= IMR_RXOVW;
	}
	if (status & IMR_FOVW) {
		print_error("RTL8139", "FIFO Overflow\n");
		_status |= IMR_FOVW;
	}
	if (status & IMR_PUN_LINKCHG) {
		print_warning("RTL8139", "Link Changed");
		_status |= IMR_PUN_LINKCHG;
	}
	if (status & IMR_LEN_CHG) {
		print_warning("RTL8139", "Length Changed");
		_status |= IMR_LEN_CHG;
	}
	if (status & IMR_TIMEOUT) {
		print_error("RTL8139", "TX Timeout\n");
		_status |= IMR_TIMEOUT;
	}
	if (status & IMR_SERR) {
		print_error("RTL8139", "System Error\n");
		_status |= IMR_SERR;
	}
	io_out_word(rtl_device->io_base + REG_ISR, _status);

end:
	spin_unlock(&rtl_device->lock);
}

void rtl8139_net_rx_handler(void *data) {
	// TODO: SMP防重入
	Rtl8139Device *device = data;
	uint8_t		   cmd	  = io_in_byte(device->io_base + RTL8139_CR);
	uint32_t	   rx_status;
	while (!(cmd & RTL8139_CR_BUFE)) {
		int i			= device->rx_offset;
		rx_status		= LE2HOST_DWORD(*(uint32_t *)(device->rx_buffer + i));
		uint16_t length = (rx_status >> 16);
		int		 packet_len = length - 4;
		i += 4;
		if (packet_len >= ETH_HEADER_SIZE &&
			(rx_status & RTL8139_RX_STAT_ROK)) {
			NetBuffer *net_buffer = net_buffer_create(packet_len);
			net_buffer_init(net_buffer, packet_len, 0, packet_len);
			void *buffer = net_buffer->ptr;

			if (i + packet_len >= RTL8139_RECV_BUF_SIZE) {
				int first_len = RTL8139_RECV_BUF_SIZE - i;
				memcpy(buffer, device->rx_buffer + i, first_len);
				memcpy(
					buffer + first_len, device->rx_buffer + i,
					packet_len - first_len);
			} else {
				memcpy(buffer, device->rx_buffer + i, packet_len);
			}
			protocol_recv(
				device->net_device, net_buffer, NETWORK_TYPE_ETHERNET);
		} else {
			printk(
				"[RTL8139]RX Error: status %#04x,size %#04x, cur %#04x\n",
				rx_status, packet_len, device->rx_offset);
		}
		device->rx_offset = (device->rx_offset + length + 4 + 3) &
							~3; // +8:4字节CRC和4字节包头；+3:4字节对齐用
		device->rx_offset &= RTL8139_RX_READ_POINTER_MASK;
		io_out_word(device->io_base + RTL8139_CAPR, device->rx_offset - 0x10);

		cmd = io_in8(device->io_base + RTL8139_CR);
	}
}

PRIVATE int mdio_read(Mii *mii, uint8_t reg_addr) {
	Rtl8139Device *device = mii->net_dev->device->private_data;
	int			   reg	  = rtl8139_mii_reg_map[reg_addr];
	if (reg == 0 || reg_addr > 8) { return 0; }

	return io_in_word(device->io_base + reg);
}

PRIVATE void mdio_write(Mii *mii, uint8_t reg_addr, uint16_t data) {
	Rtl8139Device *device = mii->net_dev->device->private_data;
	int			   reg	  = rtl8139_mii_reg_map[reg_addr];
	if (reg == 0 || reg_addr > 8) { return; }

	if (reg_addr == 0) {
		io_out_byte(device->io_base + REG_9346CR, BIT(7) | BIT(6));
		io_out_word(device->io_base + reg, data);
		io_out_byte(device->io_base + REG_9346CR, 0);
	} else {
		io_out_word(device->io_base + reg, data);
	}
}

TransferResult rtl8139_send(NetworkDevice *device, void *buf, int length) {
	Rtl8139Device *rtl_device = device->device->private_data;
	if (length > ETH_MAX_FRAME_SIZE) {
		print_error("RTL8139", "Too long packet");
		return TRANSFER_ERROR_EXCEED_MAX_SIZE;
	}

	int index = rtl_device->tx_write_idx;
	memcpy(rtl_device->tx_buffer[index], buf, length);

	int flags = spin_lock_irqsave(&rtl_device->lock);
	wmb();
	io_out_dword(
		rtl_device->io_base + REG_TSDN(index), rtl_device->tx_flag | length);
	rtl_device->tx_write_idx = (index + 1) % TX_DESC_NR;
	if (rtl_device->tx_write_idx == rtl_device->tx_done_idx) {
		// 缓冲区已满，关闭队列
		net_queue_block(&rtl_device->net_device->tx_queue, NQ_BLOCKER_DRIVER);
	}
	spin_unlock_irqrestore(&rtl_device->lock, flags);

	return TRANSFER_OK;
}

DriverResult rtl8139_init(void *_device) {
	LogicalDevice *device	  = _device;
	Rtl8139Device *rtl_device = device->private_data;
	rtl_device->io_base		  = rtl_device->pci_device->common.bar[0].base_addr;
	rtl_device->io_len		  = rtl_device->pci_device->common.bar[0].length;
	rtl_device->mmio_base	  = rtl_device->pci_device->common.bar[1].base_addr;
	rtl_device->mmio_len	  = rtl_device->pci_device->common.bar[1].length;
	rtl_device->net_device	  = device->dm_ext;
	rtl_device->mii.net_dev	  = device->dm_ext;
	rtl_device->mii.mdio_read = mdio_read;
	rtl_device->mii.mdio_write = mdio_write;
	timer_init(&rtl_device->timer);
	SPINLOCK_INIT(rtl_device->lock);

	NetworkDevice *net = device->dm_ext;
	net->mtu		   = ETH_MTU;

	return DRIVER_OK;
}

void rtl8139_reset(Rtl8139Device *device) {
	io_out_byte(device->io_base + REG_CR, CR_RST);

	uint32_t counter = timer_get_counter() + timer_count_ms(&device->timer, 10);
	while (timer_get_counter() < counter &&
		   (io_in_byte(device->io_base + REG_CR) & CR_RST)) {
		// 等待重置完成
	}
}

DriverResult rtl8139_start(void *_device) {
	LogicalDevice *device	  = _device;
	Rtl8139Device *rtl_device = device->private_data;
	pci_enable_bus_mastering(rtl_device->pci_device);

	if (rtl_device->chipset >= RTL8139B) {
		uint8_t config1 = io_in_byte(rtl_device->io_base + REG_CONFIG1);
		config1 |= CFG1_PMEN;
		// 使CONFIG寄存器可写
		io_out_byte(rtl_device->io_base + REG_9346CR, BIT(7) | BIT(6));
		io_out_byte(rtl_device->io_base + REG_CONFIG1, config1);
		// 恢复正常模式
		io_out_byte(rtl_device->io_base + REG_9346CR, 0);
	} else {
		uint8_t data = io_in_byte(rtl_device->io_base + REG_CONFIG1);
		data &= ~(CFG1_SLEEP | CFG1_PWRDN);
		io_out_byte(rtl_device->chipset, data);
	}

	rtl8139_reset(rtl_device);

	uint8_t mac_addr[6];
	for (int i = 0; i < 6; i++) {
		mac_addr[i] = io_in_byte(rtl_device->io_base + REG_IDRN(i));
	}
	NetworkDevice *net_device = device->dm_ext;
	eth_set_mac_address(net_device->ethernet, mac_addr);
	print_info(
		"RTL8139", "MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", mac_addr[0],
		mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

	net_init_mii(&rtl_device->mii);

	// 启用RX和TX
	io_out_byte(rtl_device->io_base + REG_CR, CR_RE | CR_TE);

	// 配置接收缓冲区
	rtl_device->rx_buffer	  = kernel_alloc_pages(2 << RECV_BUF_LEN);
	rtl_device->rx_buffer_phy = vir2phy((size_t)rtl_device->rx_buffer);
	io_out_dword(rtl_device->io_base + REG_RBSTART, rtl_device->rx_buffer_phy);
	io_out_dword(
		rtl_device->io_base + REG_RCR, RXFTH_NONE | RBLEN | MXDMA | RCR_AER |
										   RCR_AR | RCR_WRAP | RCR_AB | RCR_AM |
										   RCR_APM | RCR_AAP);

	// 配置发送缓冲区
	rtl_device->tx_flag = ((256) >> 5) << 16;
	io_out_dword(rtl_device->io_base + REG_TCR, MXDMA | TCR_TXRR(2));
	for (int i = 0; i < TX_DESC_NR; i++) {
		rtl_device->tx_buffer[i] = kmalloc(ETH_MAX_FRAME_SIZE);
		rtl_device->tx_buffer_phy[i] =
			vir2phy((size_t)rtl_device->tx_buffer[i]);
		io_out_dword(
			rtl_device->io_base + REG_TSADN(i), rtl_device->tx_buffer_phy[i]);
	}

	// 清空丢包计数器
	io_out_word(rtl_device->io_base + REG_MPC, 0);

	// 允许接收所有多播
	io_out_dword(rtl_device->io_base + REG_MARN(0), 0xffffffff);
	io_out_dword(rtl_device->io_base + REG_MARN(4), 0xffffffff);

	// 配置中断
	io_out_word(
		rtl_device->io_base + REG_IMR,
		IMR_ROK | IMR_RER | IMR_TOK | IMR_TER | IMR_RXOVW | IMR_PUN_LINKCHG |
			IMR_FOVW | IMR_LEN_CHG | IMR_TIMEOUT | IMR_SERR);

	register_device_irq(
		&rtl_device->irq, device->physical_device, rtl_device,
		rtl_device->pci_device->irqline, rtl8139_handler, IRQ_MODE_SHARED);

	rtl_device->net_rx.handler = rtl8139_net_rx_handler;
	rtl_device->net_rx.data	   = rtl_device;
	network_softirq_register(&rtl_device->net_rx);

	enable_device_irq(rtl_device->irq);

	return DRIVER_OK;
}

DriverResult rtl8139_pci_probe(
	PciDevice *pci_device, PhysicalDevice *physical_device) {
	uint32_t	   io_base = pci_device->common.bar[0].base_addr;
	uint32_t	   data	   = io_in_dword(io_base + RTL8139_TCR);
	Rtl8139Chipset chipset = RTL_UNKNOWN;

	data &= HWVERID_MASK;
	for (int i = 0; i < sizeof(hwrevid) / sizeof(uint32_t) / 2; i++) {
		if (data == hwrevid[i][1]) {
			chipset = hwrevid[i][0];
			break;
		}
	}
	if (chipset == RTL_UNKNOWN) { return DRIVER_ERROR_UNSUPPORT_DEVICE; }

	Rtl8139Device *rtl_device = kzalloc(sizeof(Rtl8139Device));
	rtl_device->pci_device	  = pci_device;
	rtl_device->chipset		  = chipset;

	DriverResult   result;
	NetworkDevice *network_device;
	result = create_network_device(
		&network_device, NETWORK_TYPE_ETHERNET, rtl8139_caps,
		&rtl8139_net_device_ops, &rtl8139_logical_device_ops, physical_device,
		&rtl8139_device_driver);
	if (result != DRIVER_OK) {
		kfree(rtl_device);
		return result;
	}
	register_physical_device(physical_device, &rtl8139_physical_device_ops);

	rtl_device->net_device				 = network_device;
	network_device->device->private_data = rtl_device;

	return DRIVER_OK;
}

static __init void rtl8139_initcall(void) {
	register_driver(&rtl8139_driver);
	register_device_driver(&rtl8139_driver, &rtl8139_device_driver);
	pci_register_driver(&rtl8139_driver, &rtl8139_pci_driver);
}

driver_initcall(rtl8139_initcall);