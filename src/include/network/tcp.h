#ifndef TCP_H
#define TCP_H

#include "../stdint.h"
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
#define TCP_MSL_MS			2 * 60 * 100

#define TCP_PORT_NR 65536

typedef struct {
	uint16_t src_port, dst_port;
	uint32_t seq;
	uint32_t ack;
	uint8_t	 offset;
	uint8_t	 flags;
	uint16_t window;
	uint16_t checksum;
	uint16_t urgent_pointer;
} __attribute__((packed)) tcp_header_t;

typedef struct {
	uint8_t *mem;
	uint32_t size;
	uint32_t idx_write;
	uint32_t idx_send;
	uint32_t idx_ack;
} tcp_window_t;

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

	tcp_window_t  rwin, wwin;
	tcp_status_t  status;
	uint32_t	  wait_ms, seq, ack;
	struct timer *timer;
	struct fifo	  fifo;
	int			  fifo_buf[2];
} tcp_conn_t;

#define PROTOCOL_TCP 0x06
void tcp_create(netc_t *netc);
int	 tcp_bind(netc_t *netc, uint8_t ip[4], uint16_t src_port, uint16_t dst_port);
int	 tcp_ipv4_connect(netc_t *netc);
void tcp_ipv4_close(netc_t *netc);
void tcp_read(uint8_t *buf, uint16_t offset, uint16_t length);

#endif