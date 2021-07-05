#include <device/8042.h>
#include <kernel/func.h>

int i8042_get_status(uint8_t type)
{
	int data = io_in8(I8042_PORT_STAT);
	return data&type;
}

void i8042_wait_ctr_send_ready(void)
{
	for (;;)
	{
		if((io_in8(I8042_PORT_STAT) & I8042_STAT_INBUF) == 0) {		//输入缓存区为空
			return;
		}
	}
}

void i8042_send_cmd(int command)
{
	i8042_wait_ctr_send_ready();
	io_out8(I8042_PORT_CMD, command);
}

int i8042_read_data(void)
{
	return io_in8(I8042_PORT_DATA);
}

void i8042_write_data(int data)
{
	io_out8(I8042_PORT_DATA, data);
}
