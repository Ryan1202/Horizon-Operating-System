#ifndef TCP_H
#define TCP_H

#include "driver/timer_dm.h"
#include "kernel/list.h"
#include "network.h"
#include <kernel/fifo.h>

#define TCP_FLAG_FIN  0x01
#define TCP_FLAG_SYN  0x02
#define TCP_FLAG_RST  0x04
#define TCP_FLAG_PSH  0x08
#define TCP_FLAG_ACK  0x10
#define TCP_FLAG_URG  0x20
#define TCP_FLAG_ECE  0x40
#define TCP_FLAG_CWRC 0x80

#define TCP_MAX_WINDOW_SIZE 65536
#define TCP_MSL_MS			2 * 60 * 1000

#define TCP_OPTION_END 0
#define TCP_OPTION_NOP 1
#define TCP_OPTION_MSS 2

#define TCP_PORT_NR 65536

typedef struct tcp_header_s {
	uint16_t src_port, dst_port;
	uint32_t seq;
	uint32_t ack;
	uint8_t	 offset;
	uint8_t	 flags;
	uint16_t window;
	uint16_t checksum;
	uint16_t urgent_pointer;
} __attribute__((packed)) tcp_header_t;

typedef enum {
	TCP_STAT_CLOSED = 0,
	TCP_STAT_SYN_SENT,
	TCP_STAT_ESTABLISHED,
	TCP_STAT_LISTEN,
	TCP_STAT_SYN_RCVD,
	TCP_STAT_FIN_WAIT1,
	TCP_STAT_FIN_WAIT2,
	TCP_STAT_CLOSING,
	TCP_STAT_TIME_WAIT,
	TCP_STAT_CLOSE_WAIT,
	TCP_STAT_LAST_ACK,
} tcp_status_t;

typedef struct {
	struct netc_s *netc;
	uint16_t	   src_port, dst_port;
	uint8_t		   dst_ip[4];
	list_t		   list;

	uint8_t *header_buf;

	uint16_t mss;

	struct recv_win {
		uint8_t *mem;
		uint32_t size;
		uint32_t head, tail; // 读窗口的头尾指针
		uint32_t unacked;	 // 待确认的数据大小
		uint32_t acked;		 // 已确认未读取的数据大小
		uint32_t recved;	 // 已接收的数据大小
	} rwin;

	struct send_win {
		uint8_t *mem;
		uint32_t size;
		uint32_t head, tail; // 写窗口的头尾指针
		uint32_t unacked;	 // 未确认的数据大小
		uint32_t sended;	 // 已发送的数据大小
	} swin;

	tcp_status_t status;
	uint32_t	 wait_ms, seq, ack;
	Timer		 timer;
} tcp_conn_t;

#define PROTOCOL_TCP 0x06
void tcp_create(netc_t *netc);
int	 tcp_bind(netc_t *netc, uint16_t src_port);
int	 tcp_ipv4_connect(netc_t *netc, uint8_t *ip, uint16_t dst_port);
void tcp_ipv4_close(netc_t *netc);
void tcp_recv(
	uint8_t *buf, uint16_t offset, uint16_t length, uint8_t *ip,
	uint8_t ip_len);
int tcp_write(netc_t *netc, uint8_t *buf, uint32_t length);
int tcp_read(netc_t *netc, uint8_t *buf, uint32_t length);

#endif