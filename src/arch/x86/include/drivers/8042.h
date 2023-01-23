#ifndef _8042_H
#define _8042_H

#include <stdint.h>

#define I8042_PORT_DATA 0x60
#define I8042_PORT_STAT 0x64
#define I8042_PORT_CMD  0x64

// 配置
#define I8042_CONFIG_READ     0x20
#define I8042_CONFIG_WRITE    0x60
#define I8042_CONFIG_INT1     0x01
#define I8042_CONFIG_INT2     0x02
#define I8042_CONFIG_SYS_FLAG 0x04
#define I8042_CONFIG_CLK1     0x10
#define I8042_CONFIG_CLK2     0x20
#define I8042_CONFIG_TRANS1   0x40

// 状态
#define I8042_STAT_OUTBUF      0x01
#define I8042_STAT_INBUF       0x02
#define I8042_STAT_SYSFLAG     0x04
#define I8042_STAT_DATA_TYPE   0x08
#define I8042_STAT_TIMEOUT_ERR 0x40
#define I8042_STAT_PARITY_ERR  0x80

int  i8042_get_status(uint8_t type); // 获取控制器状态
void i8042_wait_ctr_send_ready(void); // 等待输入缓存区为空
void i8042_send_cmd(int command); // 发送控制字节
int  i8042_read_data(void); // 读取数据
void i8042_write_data(int data); // 写数据

#endif