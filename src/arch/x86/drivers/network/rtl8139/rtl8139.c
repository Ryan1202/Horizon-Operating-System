#include <bits.h>
#include <driver/interrupt_dm.h>
#include <driver/network/ethernet/ethernet.h>
#include <driver/network/mii.h>
#include <driver/network/network_dm.h>
#include <driver/timer_dm.h>
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
#include <kernel/spinlock.h>
#include <objects/object.h>
#include <stdint.h>
#include <string.h>
#include <types.h>

#include "driver/network/buffer.h"
#include "driver/network/net_queue.h"
#include "driver/network/network.h"
#include "kernel/softirq.h"
#include "kernel/thread.h"
#include "objects/transfer.h"
#include "rtl8139.h"

DriverResult   rtl8139_init(Device *device);
DriverResult   rtl8139_start(Device *device);
DriverResult   rtl8139_pci_probe(PciDevice *pci_device);
TransferResult rtl8139_send(NetworkDevice *device, void *buf, int length);

DeviceDriverOps rtl8139_device_driver_ops = {
	.device_driver_init	  = NULL,
	.device_driver_uninit = NULL,
};
PciDriverOps rtl8139_pci_driver_ops = {
	.probe = rtl8139_pci_probe,
};
DeviceOps rtl8139_device_ops = {
	.init	 = rtl8139_init,
	.start	 = rtl8139_start,
	.destroy = NULL,
	.status	 = NULL,
	.stop	 = NULL,
};
NetworkDeviceOps rtl8139_net_device_ops = {
	.send = rtl8139_send,
};

DriverDependency rtl8139_dependencies[] = {
	{
		.in_type		   = DRIVER_DEPENDENCY_TYPE_BUS,
		.dependency_in_bus = {BUS_TYPE_PCI, 0},
		.out_bus		   = NULL,
	 },
};
Driver rtl8139_driver = {
	.short_name		  = STRING_INIT("HorizonRtl8139Driver"),
	.dependency_count = sizeof(rtl8139_dependencies) / sizeof(DriverDependency),
	.dependencies	  = rtl8139_dependencies,
};
DeviceDriver rtl8139_device_driver = {
	.name	  = STRING_INIT("RTL8139"),
	.bus	  = NULL,
	.type	  = DEVICE_TYPE_ETHERNET,
	.priority = DRIVER_PRIORITY_BASIC,
	.ops	  = &rtl8139_device_driver_ops,
};
PciDriver rtl8139_pci_driver = {
	.driver		   = &rtl8139_driver,
	.device_driver = &rtl8139_device_driver,
	.find_type	   = FIND_BY_VENDORID_DEVICEID,
	.vendor_device = {RTL8139_VENDOR_ID, RTL8139_DEVICE_ID},
	.ops		   = &rtl8139_pci_driver_ops,
};
const Device rtl8139_device_template = {
	.name			   = STRING_INIT("RTL8139"),
	.device_driver	   = &rtl8139_device_driver,
	.ops			   = &rtl8139_device_ops,
	.private_data_size = sizeof(Rtl8139Device),
};
const NetworkDevice rtl8139_network_device_template = {
	.head_size = 0,
	.tail_size = 0,
	.type	   = NETWORK_TYPE_ETHERNET,
	.ops	   = &rtl8139_net_device_ops,
};

void rtl8139_handler(Device *device) {
	Rtl8139Device *rtl_device = device->private_data;

	spin_lock(&rtl_device->lock);
	int status = io_in_word(rtl_device->io_base + REG_ISR);

	if (status == 0xffff) goto end;

	io_out_word(rtl_device->io_base + REG_ISR, status);
	if (status & IMR_TOK) {
		rtl_device->tx_done_idx = (rtl_device->tx_done_idx + 1) % TX_DESC_NR;
	}
	if (status & IMR_ROK) pending_softirq();
	if (status & IMR_RXOVW) print_error("RTL8139", "RX Overflow\n");
	if (status & IMR_FOVW) print_error("RTL8139", "FIFO Overflow\n");
	if (status & IMR_PUN_LINKCHG) { print_device_info(device, "Link Changed"); }

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
		uint16_t length = (rx_status >> 16) - 4;
		i += 4;
		if (length >= ETH_HEADER_SIZE && (rx_status & RTL8139_RX_STAT_ROK)) {
			NetBuffer *net_buffer = net_buffer_create(length);
			net_buffer_init(net_buffer, length, 0, length);
			void *buffer = net_buffer->ptr;

			if (i + length >= RTL8139_RECV_BUF_SIZE) {
				memcpy(
					buffer, device->rx_buffer + i, RTL8139_RECV_BUF_SIZE - i);
				i = RTL8139_RECV_BUF_SIZE - i;
				memcpy(buffer + i, device->rx_buffer + i, length - i);
			} else {
				memcpy(buffer, device->rx_buffer + i, length);
			}
			eth_recv(net_buffer);
		} else {
			printk(
				"[RTL8139]RX Error: status %#04x,size %#04x, cur %#04x\n",
				rx_status, length + 4, device->rx_offset);
		}
		device->rx_offset = (device->rx_offset + length + 8 + 3) &
							~3; // +8:4字节CRC和4字节包头；+3:4字节对齐用
		device->rx_offset %= RTL8139_RECV_BUF_SIZE;
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

DriverResult rtl8139_init(Device *device) {
	Rtl8139Device *rtl_device = device->private_data;
	rtl_device->io_base		  = rtl_device->pci_device->common.bar[0].base_addr;
	rtl_device->io_len		  = rtl_device->pci_device->common.bar[0].length;
	rtl_device->mmio_base	  = rtl_device->pci_device->common.bar[1].base_addr;
	rtl_device->mmio_len	  = rtl_device->pci_device->common.bar[1].length;
	rtl_device->net_device	  = device->dm_ext;
	rtl_device->mii.net_dev	  = device->dm_ext;
	rtl_device->mii.mdio_read = mdio_read;
	rtl_device->mii.mdio_write = mdio_write;
	rtl_device->net_device->ethernet =
		kmalloc_from_template(rtl8139_network_device_template);
	timer_init(&rtl_device->timer);
	SPINLOCK_INIT(rtl_device->lock);

	return DRIVER_RESULT_OK;
}

void rtl8139_reset(Rtl8139Device *device) {
	io_out_byte(device->io_base + REG_CR, CR_RST);

	uint32_t counter = timer_get_counter() + timer_count_ms(&device->timer, 10);
	while (timer_get_counter() < counter &&
		   (io_in_byte(device->io_base + REG_CR) & CR_RST)) {
		// 等待重置完成
	}
}

DriverResult rtl8139_start(Device *device) {
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
	print_device_info(
		device, "MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", mac_addr[0],
		mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

	net_init_mii(&rtl_device->mii);

	// 启用RX和TX
	io_out_byte(rtl_device->io_base + REG_CR, CR_RE | CR_TE);

	// 配置接收缓冲区
	rtl_device->rx_buffer	  = kmalloc(RTL8139_RECV_BUF_SIZE);
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

	device->irq			 = kmalloc(sizeof(DeviceIrq));
	device->irq->device	 = device;
	device->irq->irq	 = rtl_device->pci_device->irqline;
	device->irq->handler = rtl8139_handler;

	rtl_device->net_rx.handler = rtl8139_net_rx_handler;
	rtl_device->net_rx.data	   = rtl_device;
	network_softirq_register(&rtl_device->net_rx);

	register_device_irq(device->irq);
	interrupt_enable_irq(rtl_device->pci_device->irqline);

	return DRIVER_RESULT_OK;
}

DriverResult rtl8139_pci_probe(PciDevice *pci_device) {
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
	if (chipset == RTL_UNKNOWN) { return DRIVER_RESULT_UNSUPPORT_DEVICE; }

	Device		  *device = kmalloc_from_template(rtl8139_device_template);
	NetworkDevice *network_device =
		kmalloc_from_template(rtl8139_network_device_template);
	ObjectAttr attr = device_object_attr;
	device->bus		= pci_device->bus;
	register_network_device(
		&rtl8139_device_driver, device, network_device, &attr);

	Rtl8139Device *rtl_device = device->private_data;
	rtl_device->pci_device	  = pci_device;
	rtl_device->chipset		  = chipset;

	return DRIVER_RESULT_OK;
}

static __init void rtl8139_initcall(void) {
	register_driver(&rtl8139_driver);
	register_device_driver(&rtl8139_driver, &rtl8139_device_driver);
	pci_register_driver(&rtl8139_driver, &rtl8139_pci_driver);
}

driver_initcall(rtl8139_initcall);