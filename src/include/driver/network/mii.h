#ifndef _NETWORK_MII_H
#define _NETWORK_MII_H

#include "network_dm.h"
#include <bits.h>
#include <stdint.h>

#define MII_REG_BMCR   0x00
#define MII_REG_BMSR   0x01
#define MII_REG_PHYID1 0x02
#define MII_REG_PHYID2 0x03
#define MII_REG_ANAR   0x04
#define MII_REG_ANLPAR 0x05

#define MII_BMSR_EXT_CAP		   BIT(0)
#define MII_BMSR_JABBER_DETECTED   BIT(1)
#define MII_BMSR_LINK_STATUS	   BIT(2)
#define MII_BMSR_AUTO_NEG		   BIT(3)
#define MII_BMSR_REMOTE_FAULT	   BIT(4)
#define MII_BMSR_AUTO_NEG_COMPLETE BIT(5)
#define MII_BMSR_EXT_STAT		   BIT(8)
#define MII_BMSR_10_HALF_DUPLEX	   BIT(11)
#define MII_BMSR_10_FULL_DUPLEX	   BIT(12)
#define MII_BMSR_100_HALF_DUPLEX   BIT(13)
#define MII_BMSR_100_FULL_DUPLEX   BIT(14)
#define MII_BMSR_100_T4			   BIT(15)

typedef enum MiiMediaType {
	MMT_10HALF	= 0b00000,
	MMT_10FULL	= 0b00001,
	MMT_100HALF = 0b00010,
	MMT_100FULL = 0b00011,
	MMT_100T4	= 0b10010,
	MMT_UNKNOWN,
} MiiMediaType;

typedef struct Mii {
	NetworkDevice *net_dev;

	uint32_t phy_id;
	uint32_t full_duplex : 1; // 1: 全双工, 0: 半双工

	int (*mdio_read)(struct Mii *mii, uint8_t reg_addr);
	void (*mdio_write)(struct Mii *mii, uint8_t reg_addr, uint16_t data);
} Mii;

DriverResult net_init_mii(Mii *mii);

#endif