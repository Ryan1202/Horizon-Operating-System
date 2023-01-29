#ifndef RTL8139_H
#define RTL8139_H

#define RTL8139_IDRN(n)	   (0x00 + n)
#define RTL8139_MARN(n)	   (0x08 + n)
#define RTL8139_TSDN(n)	   (0x10 + (n << 2))
#define RTL8139_TSADN(n)   (0x20 + (n << 2))
#define RTL8139_RBSTART	   0x30
#define RTL8139_ERBCR	   0x34
#define RTL8139_ERSR	   0x36
#define RTL8139_CR		   0x37
#define RTL8139_CAPR	   0x38
#define RTL8139_CBR		   0x3a
#define RTL8139_IMR		   0x3c
#define RTL8139_ISR		   0x3e
#define RTL8139_TCR		   0x40
#define RTL8139_RCR		   0x44
#define RTL8139_TCTR	   0x48
#define RTL8139_MPC		   0x4c
#define RTL8139_9346CR	   0x50
#define RTL8139_CONFIG0	   0x51
#define RTL8139_CONFIG1	   0x52
#define RTL8139_TIR		   0x54
#define RTL8139_MSR		   0x58
#define RTL8139_CONFIG3	   0x59
#define RTL8139_CONFIG4	   0x5a
#define RTL8139_MULINT	   0x5c
#define RTL8139_RERID	   0x5e
#define RTL8139_TSAD	   0x60
#define RTL8139_BMCR	   0x62
#define RTL8139_BMSR	   0x64
#define RTL8139_ANAR	   0x66
#define RTL8139_ANLPAR	   0x68
#define RTL8139_ANER	   0x6a
#define RTL8139_DIS		   0x6c
#define RTL8139_FCSC	   0x6e
#define RTL8139_NWAYTR	   0x70
#define RTL8139_REC		   0x72
#define RTL8139_CSCR	   0x74
#define RTL8139_PHY1_PARM  0x78
#define RTL8139_TW_PARM	   0x7c
#define RTL8139_PHY2_PARM  0x80
#define RTL8139_CRCN(n)	   (0x84 + n)
#define RTL8139_WakeupN(n) (0x8c + (n << 3))
#define RTL8139_LSBCRCN(n) (0xcc + n)
#define RTL8139_CONFIG5	   0xd8

// CR 命令寄存器

#define RTL8139_CR_RST	0x10
#define RTL8139_CR_RE	0x08
#define RTL8139_CR_TE	0x04
#define RTL8139_CR_BUFE 0x01

// IMR 中断屏蔽寄存器

#define RTL8139_IMR_SERR		0x8000 // System Error
#define RTL8139_IMR_TimeOut		0x4000 // Time Out
#define RTL8139_IMR_LenChg		0x2000 // Cable Length Change
#define RTL8139_IMR_FOVW		0x0040 // Rx FIFO Overflow
#define RTL8139_IMR_PUN_LinkChg 0x0020 // Packet Underrun/Link Change
#define RTL8139_IMR_RXOVW		0x0010 // Rx Buffer Overflow
#define RTL8139_IMR_TER			0x0008 // Transmit Error
#define RTL8139_IMR_TOK			0x0004 // Transmit OK
#define RTL8139_IMR_RER			0x0002 // Receive Error
#define RTL8139_IMR_ROK			0x0001 // Receive OK

// ISR 中断状态寄存器

#define RTL8139_ISR_SERR		0x8000 // System Error
#define RTL8139_ISR_TimeOut		0x4000 // Time Out
#define RTL8139_ISR_LenChg		0x2000 // Cable Length Change
#define RTL8139_ISR_FOVW		0x0040 // Rx FIFO Overflow
#define RTL8139_ISR_PUN_LinkChg 0x0020 // Packet Underrun/Link Change
#define RTL8139_ISR_RXOVW		0x0010 // Rx Buffer Overflow
#define RTL8139_ISR_TER			0x0008 // Transmit Error
#define RTL8139_ISR_TOK			0x0004 // Transmit OK
#define RTL8139_ISR_RER			0x0002 // Receive Error
#define RTL8139_ISR_ROK			0x0001 // Receive OK

// TCR 传输配置寄存器

#define RTL8139_TCR_IFG(n)	 ((n & 0x03) << 24)
#define RTL8139_TCR_LBK(n)	 ((n & 0x03) << 17)
#define RTL8139_TCR_CRC		 0x0100
#define RTL8139_TCR_MXDMA(n) ((n & 0x07) << 8)
#define RTL8139_TCR_TXRR(n)	 ((n & 0x0f) << 4)
#define RTL8139_TCR_CLRABT	 0x0001

// RCR 接收配置寄存器

#define RTL8139_RCR_RXFTH(n) ((n & 0x07) << 13)
#define RTL8139_RCR_RBLEN(n) ((n & 0x03) << 11)
#define RTL8139_RCR_MXDMA(n) ((n & 0x07) << 8)
#define RTL8139_RCR_WRAP	 0x0080
#define RTL8139_RCR_AER		 0x0020 // Accept Error Packet
#define RTL8139_RCR_AR		 0x0010 // Accept Runt
#define RTL8139_RCR_AB		 0x0008 // Accept Broadcast packets
#define RTL8139_RCR_AM		 0x0004 // Accept Multicast packets
#define RTL8139_RCR_APM		 0x0002 // Accept Physical Match packets
#define RTL8139_RCR_AAP		 0x0001 // Accept  All Packets

#define RTL8139_TRANS_DESC()

#endif