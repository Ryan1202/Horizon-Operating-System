#ifndef _DMA_H
#define _DMA_H

#define DMA0				0x00
#define DMA1				0xc0

#define DMA_ADDR0			0x00
#define DMA_CNT0			0x01
#define DMA_ADDR1			0x02
#define DMA_CNT1			0x03
#define DMA_ADDR2			0x04
#define DMA_CNT2			0x05
#define DMA_ADDR3			0x06
#define DMA_CNT3			0x07
#define DMA_ADDR4			0xc0
#define DMA_CNT4			0xc2
#define DMA_ADDR5			0xc4
#define DMA_CNT5			0xc6
#define DMA_ADDR6			0xc8
#define DMA_CNT6			0xca
#define DMA_ADDR7			0xcc
#define DMA_CNT7			0xce

//DMA寄存器
#define DMA1_REG_STAT		0x08	//状态寄存器
#define DMA1_REG_CMD		0x08	//命令寄存器
#define DMA1_REG_REQ		0x09	//请求寄存器
#define DMA1_REG_MASK		0x0a	//屏蔽寄存器
#define DMA1_REG_MODE		0x0b	//模式寄存器
#define DMA1_REG_FF_RESET	0x0c	//触发器复位寄存器
#define DMA1_REG_TEMP		0x0d	//暂存寄存器
#define DMA1_REG_RESET		0x0d	//主DMA复位寄存器
#define DMA1_REG_MASK_RESET	0x0e	//复位屏蔽寄存器
#define DMA1_REG_ALL_MASK	0x0f	//多通道屏蔽寄存器

#define DMA2_REG_STAT		0xd0	//状态寄存器
#define DMA2_REG_CMD		0xd0	//命令寄存器
#define DMA2_REG_REQ		0xd2	//请求寄存器
#define DMA2_REG_MASK		0xd4	//屏蔽寄存器
#define DMA2_REG_MODE		0xd6	//模式寄存器
#define DMA2_REG_FF_RESET	0xd8	//触发器复位寄存器
#define DMA2_REG_TEMP		0xda	//暂存寄存器
#define DMA2_REG_RESET		0xda	//DMA复位寄存器
#define DMA2_REG_MASK_RESET	0xdc	//复位屏蔽寄存器
#define DMA2_REG_ALL_MASK	0xde	//多通道屏蔽寄存器

//屏蔽寄存器
#define DMA_MASK_ON			0x04

//DMA页寄存器
#define DMA_PAGE0			0x87
#define DMA_PAGE1			0x83
#define DMA_PAGE2			0x81
#define DMA_PAGE3			0x82
#define DMA_PAGE5			0x8b
#define DMA_PAGE6			0x89
#define DMA_PAGE7			0x8a

#define DMA_MODE_READ		0x06
#define DMA_MODE_WRITE		0x0a

void dma_enable(unsigned int channel);
void dma_disable(unsigned int channel);
void dma_ff_reset(unsigned int channel);
void dma_set_mode(unsigned int channel, char mode);
void dma_set_page(unsigned int channel, char page);
void dma_set_addr(unsigned int channel, unsigned int addr);
void dma_set_count(unsigned int channel, unsigned int count);

#endif