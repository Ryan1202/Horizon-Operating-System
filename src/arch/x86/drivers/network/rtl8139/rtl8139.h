#ifndef _RTL8139_H
#define _RTL8139_H

#include "kernel/driver_interface.h"
#include <bits.h>
#include <driver/network/mii.h>
#include <driver/network/network.h>
#include <driver/network/network_dm.h>
#include <driver/timer/timer_dm.h>
#include <drivers/bus/pci/pci.h>
#include <kernel/spinlock.h>
#include <stdint.h>

#define RTL8139_VENDOR_ID 0x10ec
#define RTL8139_DEVICE_ID 0x8139

#define TX_DESC_NR 4

// IDR和MAR寄存器都要按4个字节读写
#define REG_IDRN(n)	 (0 + (n))
#define REG_MARN(n)	 (8 + (n))
#define REG_TSDN(n)	 (0x10 + ((n) << 2))
#define REG_TSADN(n) (0x20 + ((n) << 2))
#define REG_RBSTART	 0x30
#define REG_CR		 0x37
#define REG_IMR		 0x3c
#define REG_ISR		 0x3e
#define REG_TCR		 0x40
#define REG_RCR		 0x44
#define REG_MPC		 0x4c
#define REG_9346CR	 0x50
#define REG_CONFIG0	 0x51
#define REG_CONFIG1	 0x52
#define REG_BMCR	 0x62
#define REG_BMSR	 0x64
#define REG_ANAR	 0x66
#define REG_ANLPAR	 0x68
#define REG_ANER	 0x6a

#define CR_BUFE BIT(0)
#define CR_TE	BIT(2)
#define CR_RE	BIT(3)
#define CR_RST	BIT(4)

#define CFG1_DVRLOAD BIT(5)
#define CFG1_LWACT	 BIT(4)
#define CFG1_MEMMAP	 BIT(3)
#define CFG1_IOMAP	 BIT(2)
#define CFG1_VPD	 BIT(1)
#define CFG1_PMEN	 BIT(0)

#define IMR_ROK			BIT(0)
#define IMR_RER			BIT(1)
#define IMR_TOK			BIT(2)
#define IMR_TER			BIT(3)
#define IMR_RXOVW		BIT(4)
#define IMR_PUN_LINKCHG BIT(5)
#define IMR_FOVW		BIT(6)
#define IMR_LEN_CHG		BIT(13)
#define IMR_TIMEOUT		BIT(14)
#define IMR_SERR		BIT(15)

/**
 * 有资料提到RTL8139和RTL8139A没有CONFIG1寄存器，
 * 然而QEMU和Linux代码中都提到了对于这两个芯片存在SLEEP和PWRDN这两位，
 * 但我没找到相关文档，只能先写上
 */
#define CFG1_SLEEP BIT(1)
#define CFG1_PWRDN BIT(0)

#define RTL8139_HWVERID(a, b, c, d, e, f, g) \
	(a << 30 | b << 29 | c << 28 | d << 27 | e << 26 | f << 23 | g << 22)
#define HWVERID_MASK RTL8139_HWVERID(1, 1, 1, 1, 1, 1, 1)

#define IMR_ALL                                                   \
	RTL8139_IMR_SERR | RTL8139_IMR_TimeOut | RTL8139_IMR_LenChg | \
		RTL8139_IMR_FOVW | RTL8139_IMR_TER | RTL8139_IMR_TOK |    \
		RTL8139_IMR_RER | RTL8139_IMR_ROK

#define RCR_RXFTH(n) ((n & 0x07) << 13)
#define RCR_RBLEN(n) ((n & 0x03) << 11)
#define RCR_MXDMA(n) ((n & 0x07) << 8)
#define RCR_WRAP	 BIT(7)
#define RCR_AER		 BIT(5)
#define RCR_AR		 BIT(4)
#define RCR_AB		 BIT(3)
#define RCR_AM		 BIT(2)
#define RCR_APM		 BIT(1)
#define RCR_AAP		 BIT(0)

#define TCR_CRC		BIT(16)
#define TCR_TXRR(n) ((n & 0x0f) << 4)
#define TCR_CLRABT	BIT(0)

#define RBLEN_64K 0x03
#define RBLEN_32K 0x02
#define RBLEN_16K 0x01
#define RBLEN_8K  0x00

#define MXDMA_16B		0b000
#define MXDMA_32B		0b001
#define MXDMA_64B		0b010
#define MXDMA_128B		0b011
#define MXDMA_256B		0b100
#define MXDMA_512B		0b101
#define MXDMA_1024B		0b110
#define MXDMA_UNLIMITED 0b111

#define MXDMA		 RCR_MXDMA(MXDMA_1024B)
#define RXFTH_NONE	 RCR_RXFTH(0x07)
#define RECV_BUF_LEN RBLEN_64K
#define RBLEN		 RCR_RBLEN(RECV_BUF_LEN)

#define RTL8139_RECV_BUF_SIZE		 (8192 << RECV_BUF_LEN)
#define RTL8139_RX_READ_POINTER_MASK (((8192 << RECV_BUF_LEN) - 1) & ~3)

typedef enum Rtl8139Chipset {
	RTL8139 = 0,
	RTL8139A,
	RTL8139AG,
	RTL8139B,
	RTL8130,
	RTL8139C,
	RTL8100,
	RTL8139D,
	RTL8139CP,
	RTL8101,
	RTL_UNKNOWN,
} Rtl8139Chipset;

typedef struct Rtl8139Device {
	Timer		   timer;
	PciDevice	  *pci_device;
	NetworkDevice *net_device;
	uint32_t	   io_base, io_len;
	uint32_t	   mmio_base, mmio_len;
	Rtl8139Chipset chipset;
	Mii			   mii;
	spinlock_t	   lock;
	NetRxHandler   net_rx;
	DeviceIrq	  *irq;

	void	*rx_buffer;
	size_t	 rx_buffer_phy;
	uint32_t rx_offset;

	uint32_t tx_flag;
	uint8_t	 tx_write_idx;
	uint8_t	 tx_done_idx;
	void	*tx_buffer[TX_DESC_NR];
	size_t	 tx_buffer_phy[TX_DESC_NR];
} Rtl8139Device;

static const int hwrevid[][2] = {
	{	 RTL8139, RTL8139_HWVERID(1, 1, 0, 0, 0, 0, 0)},
	{ RTL8139A, RTL8139_HWVERID(1, 1, 1, 0, 0, 0, 0)},
	{RTL8139AG, RTL8139_HWVERID(1, 1, 1, 0, 1, 0, 0)},
	{ RTL8139B, RTL8139_HWVERID(1, 1, 1, 1, 0, 0, 0)},
	{	 RTL8130, RTL8139_HWVERID(1, 1, 1, 1, 0, 0, 0)},
	{ RTL8139C, RTL8139_HWVERID(1, 1, 1, 0, 1, 0, 0)},
	{RTL8139CP, RTL8139_HWVERID(1, 1, 1, 0, 1, 1, 0)},
	{	 RTL8100, RTL8139_HWVERID(1, 1, 1, 1, 0, 1, 0)},
	{ RTL8139D, RTL8139_HWVERID(1, 1, 1, 0, 1, 0, 1)},
	{	 RTL8101, RTL8139_HWVERID(1, 1, 1, 0, 1, 1, 1)},
};

static const int rtl8139_mii_reg_map[] = {
	REG_BMCR,	// MII_BMCR
	REG_BMSR,	// MII_BMSR
	0,			// MII_PHYID1
	0,			// MII_PHYID2
	REG_ANAR,	// MII_ANAR
	REG_ANLPAR, // MII_ANLPAR
	REG_ANER,	// MII_ANER
	0,			// MII_ANNPTR
};

#endif