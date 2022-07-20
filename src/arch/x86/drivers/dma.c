/**
 * @file dma.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief (ISA)DMA驱动
 * @version 0.1
 * @date 2021-7
 */
#include <drivers/dma.h>
#include <kernel/func.h>

void dma_enable(unsigned int channel)
{
	if (channel < 4)
	{
		io_out8(DMA1_REG_MASK, channel);
	}
	else
	{
		io_out8(DMA2_REG_MASK, channel % 4);
	}
}

void dma_disable(unsigned int channel)
{
	if (channel < 4)
	{
		io_out8(DMA1_REG_MASK, channel | 4);
	}
	else
	{
		io_out8(DMA2_REG_MASK, (channel % 4) | 4);
	}
}

void dma_ff_reset(unsigned int channel)
{
	if (channel < 4)
	{
		io_out8(DMA1_REG_FF_RESET, 0);
	}
	else
	{
		io_out8(DMA2_REG_FF_RESET, 0);
	}
}

void dma_set_mode(unsigned int channel, char mode)
{
	if (channel < 4)
	{
		io_out8(DMA1_REG_MODE, mode | channel);
	}
	else
	{
		io_out8(DMA2_REG_MODE, mode | (channel % 4));
	}
}

void dma_set_page(unsigned int channel, char page)
{
	switch (channel)
	{
	case 0:
		io_out8(DMA_PAGE0, page);
		break;
	case 1:
		io_out8(DMA_PAGE1, page);
		break;
	case 2:
		io_out8(DMA_PAGE2, page);
		break;
	case 3:
		io_out8(DMA_PAGE3, page);
		break;
	case 5:
		io_out8(DMA_PAGE5, page & 0xfe);
		break;
	case 6:
		io_out8(DMA_PAGE6, page & 0xfe);
		break;
	case 7:
		io_out8(DMA_PAGE7, page & 0xfe);
		break;
	
	default:
		break;
	}
}

void dma_set_addr(unsigned int channel, unsigned int addr)
{
	dma_set_page(channel, addr>>16);
	if (channel <= 3)  {
		io_out8(DMA0 + (channel<<1), addr & 0xff);
		io_out8(DMA0 + (channel<<1), (addr>>8) & 0xff);
	}  else  {
		io_out8(DMA1 + ((channel&3)<<2), (addr>>1) & 0xff);
		io_out8(DMA1 + ((channel&3)<<2), (addr>>9) & 0xff);
	}
}

void dma_set_count(unsigned int channel, unsigned int count)
{
	count--;
	if (channel <= 3)  {
		io_out8(DMA0 + (channel<<1) + 1, count & 0xff);
		io_out8(DMA0 + (channel<<1) + 1, (count>>8) & 0xff);
	}  else  {
		io_out8(DMA1 + ((channel&3)<<2) + 2, (count>>1) & 0xff);
		io_out8(DMA1 + ((channel&3)<<2) + 2, (count>>9) & 0xff);
	}
}
