#include "kernel/console.h"
#include "kernel/driver_interface.h"
#include <driver/network/mii.h>
#include <driver/network/network_dm.h>
#include <kernel/driver.h>
#include <stdint.h>

bool mii_is_linked(Mii *mii) {
	uint32_t data = mii->mdio_read(mii, MII_REG_BMSR);
	return !!(data & MII_BMSR_LINK_STATUS);
}

MiiMediaType mii_check_media_type(uint32_t negotiation) {
	MiiMediaType type = MMT_UNKNOWN;
	if (negotiation & BIT(5)) type = MMT_10HALF;
	if (negotiation & BIT(6)) type = MMT_10FULL;
	if (negotiation & BIT(7)) type = MMT_100HALF;
	if (negotiation & BIT(8)) type = MMT_100FULL;
	if (negotiation & BIT(9)) type = MMT_100T4;

	return type;
}

/**
 * @brief 通过MII接口初始化网卡配置
 *
 */
DriverResult net_init_mii(Mii *mii) {
	bool  linked	  = mii_is_linked(mii);
	char *device_name = mii->net_dev->device->object->name.text;

	if (!linked) {
		print_info("MII", "device %s No link detected\n", device_name);
		mii->net_dev->state = NET_STATE_NO_CARRIER;
		return DRIVER_OK;
	}
	print_info("MII", "device %s Link detecteds\n", device_name);
	mii->net_dev->state = NET_STATE_RUNNING;

	uint32_t	 anar	 = mii->mdio_read(mii, MII_REG_ANAR);	// 本地能力
	uint32_t	 anlpar	 = mii->mdio_read(mii, MII_REG_ANLPAR); // 对端能力
	uint32_t	 support = anar & anlpar; // 计算双方都支持的特性
	MiiMediaType media_type = mii_check_media_type(support);

	print_info("MII", "device %s Media type: ", device_name);
	int speed  = (media_type >> 1) & 0b111;
	int duplex = media_type & 1;
	if (speed == 0) printk("10Mbps ");
	else if (speed == 1) printk("100Mbps ");
	if (duplex) printk("Full Duplex");
	else printk("Half Duplex");
	printk("\n");

	mii->full_duplex = duplex;
	return DRIVER_OK;
}