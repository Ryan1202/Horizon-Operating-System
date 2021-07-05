#include <device/floppy.h>
#include <kernel/console.h>
#include <kernel/func.h>
#include <device/cmos.h>
#include <device/dma.h>
#include <kernel/fifo.h>
#include <kernel/page.h>
#include <kernel/descriptor.h>

int *floppy_buf;
struct floppys floppys;
struct fifo floppy_fifo;

void init_floppy(void)
{
	int i;
	
	printk("floppy:\n");
	io_out8(FLOPPY_DOR, FLOPPY_SEL_A | FLOPPY_DMA_INT | FLOPPY_RESET);
	floppys.type[0].cyl = 80;
	floppys.type[0].hed = 0;
	floppys.type[0].sec = 1;
	floppys.num = floppy_num() + 1;
	if (floppy_check_version() == -1)
	{
		return;
	};
	io_out8(FLOPPY_DOR, FLOPPY_EN_MOTA | FLOPPY_DMA_INT);
	io_out8(FLOPPY_CCR, 0);

	floppy_buf = (int *)kmalloc(4096);
	fifo_init(&floppy_fifo, 4096, &floppy_fifo);
	
	dma_disable(FLOPPY_DMA);
	dma_ff_reset(FLOPPY_DMA);
	dma_set_mode(FLOPPY_DMA, DMA_MODE_WRITE);
	dma_set_addr(FLOPPY_DMA, FLOPPY_DMABUF);
	dma_set_count(FLOPPY_DMA, floppys.type[0].sec * floppys.type[0].hed * 512);
	dma_enable(FLOPPY_DMA);
	io_out8(FLOPPY_DOR, 0x08);
	for (i=0 ; i<1000 ; i++)
		__asm__("nop");
	io_out8(FLOPPY_DOR, 0x0c);
	put_irq_handler(FLOPPY_IRQ, floppy_handler);
	enable_irq(FLOPPY_IRQ);
}

int floppy_num(void)		//获取软驱数量
{
	return cmos_read(CMOS_EQP)>>6;
}

int floppy_check_version(void)
{
	int data;
	
	send_cmd(FLOPPY_VERSION);
	data = recv_data();
	if (data == 0x90)
	{
		return 0;
	}
	return -1;
}

void send_cmd(char cmd)
{
	int i;
	int status;
	
	for (i = 0; i < 10000; i++)
	{
		status = io_in8(FLOPPY_STATUS);
		
		if((status & 0xc0) == 0x80/*(status & FLOPPY_READY) && (status & FLOPPY_IO_DIR) && (status & FLOPPY_BUSY_FDC)==0*/)
		{
			io_out8(FLOPPY_DATA, cmd);
			return;
		}
	}
}

int recv_data(void)
{
	int i;
	int status;
	
	for (i = 0; i < 10000; i++)
	{
		status = io_in8(FLOPPY_STATUS) & (FLOPPY_IO_DIR | FLOPPY_READY);
		if (status == (FLOPPY_IO_DIR | FLOPPY_READY)) {
			return io_in8(FLOPPY_DATA);
		}
	}
	return -1;
}

void wait_fdc_ready()
{
	int i;
	int status;
	
	for (i = 0; i < 10000; i++)
	{
		status = io_in8(FLOPPY_STATUS);
		if ((status & FLOPPY_BUSY_FDC) == 0) {
			return;
		}
	}
}

void wait_fd_ready(int fdnum)
{
	int i;
	int status, disk = FLOPPY_BUSY_FDC;
	switch (fdnum)
	{
	case 0:
		disk |= FLOPPY_BUSY_A;
		break;
	case 1:
		disk |= FLOPPY_BUSY_B;
		break;
	case 2:
		disk |= FLOPPY_BUSY_C;
		break;
	case 3:
		disk |= FLOPPY_BUSY_D;
		break;
	default:
		break;
	}
	for (i = 0; i < 10000; i++)
	{
		status = io_in8(FLOPPY_STATUS);
		if ((status & disk) == 0) {
			return;
		}
	}
}

void floppy_handler(int irq)
{
	fifo_put(&floppy_fifo, 255);
}

void floppy_test_read(void)
{
	int i;
	struct timer *timer;
	timer = timer_alloc();
	timer_init(timer, &floppy_fifo, 128);
	timer_settime(timer, 300);
	
	for (;;)
	{
		if (fifo_status(&floppy_fifo) != 0)
		{
			i = fifo_get(&floppy_fifo);
			if (i == 128)
			{
				wait_fd_ready(0);
				send_cmd(FLOPPY_SEEK);
				send_cmd(floppys.type[0].hed << 2);
				send_cmd(floppys.type[0].cyl);
			}
			else if (i == 255)
			{
				
			}
			
		}
	}
}