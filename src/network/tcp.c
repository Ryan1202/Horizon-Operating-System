#include "kernel/list.h"
#include "kernel/memory.h"
#include "network/netpack.h"
#include <drivers/pit.h>
#include <kernel/func.h>
#include <math.h>
#include <network/ipv4.h>
#include <network/network.h>
#include <network/tcp.h>
#include <stdint.h>
#include <stdlib.h>

#include <kernel/console.h>

SPINLOCK(tcp_lock);
LIST_HEAD(tcp_lh);

const int rmem_defualt_size = 32768;
const int wmem_defualt_size = 16384;

uint16_t tcp_checksum(netc_t *netc, uint8_t dst_ip[4], uint8_t *buf, uint32_t length) {
	int		 i;
	uint8_t	 src_ip[4];
	uint32_t chksum = 0;
	ipv4_get_ip(netc, src_ip);
	chksum += (src_ip[0] << 8) + src_ip[1];
	chksum += (src_ip[2] << 8) + src_ip[3];
	chksum += (dst_ip[0] << 8) + dst_ip[1];
	chksum += (dst_ip[2] << 8) + dst_ip[3];
	chksum += PROTOCOL_TCP;
	chksum += length;
	for (i = 0; i < length / sizeof(uint16_t); i++) {
		chksum += (buf[i * 2] << 8) + buf[i * 2 + 1];
	}
	if (length % sizeof(uint16_t)) { chksum += buf[length - 1] << 8; }
	while (chksum & 0xffff0000) {
		chksum = (chksum >> 16) + (chksum & 0xffff);
	}

	return (~chksum) & 0xffff;
}

int tcp_send_for_status_change(netc_t *netc, uint8_t *buf, int length) {
	tcp_conn_t  *conn	= (tcp_conn_t *)netc->app_private;
	tcp_status_t status = conn->status;

	netc_ip_send(netc, conn->dst_ip, 0, PROTOCOL_TCP, buf, length);

	conn->wait_ms = 600;
	conn->timer	  = timer_alloc();
	timer_init(conn->timer, &conn->fifo, 0);
	fifo_init(&conn->fifo, 2, conn->fifo_buf);
	timer_settime(conn->timer, conn->wait_ms / 10);
	while (conn->status == status) {
		if (fifo_status(&conn->fifo)) {
			while (fifo_status(&conn->fifo))
				fifo_get(&conn->fifo);
			conn->wait_ms *= 2;
			if (conn->wait_ms >= 120000) { return -1; }
			netc_ip_send(netc, conn->dst_ip, 0, PROTOCOL_TCP, buf, length);
			timer_settime(conn->timer, conn->wait_ms / 10);
		}
	}
	timer_free(conn->timer);
	if (fifo_status(&conn->fifo)) fifo_get(&conn->fifo);
	return 0;
}

int tcp_send(netc_t *netc, uint8_t *buf, int length) {
	tcp_conn_t   *conn	  = (tcp_conn_t *)netc->app_private;
	uint8_t		*sendbuf = kmalloc(sizeof(tcp_header_t) + length);
	tcp_header_t *header  = (tcp_header_t *)sendbuf;

	memcpy(header, conn->header_buf, sizeof(tcp_header_t));

	header->seq			   = HOST2BE_DWORD(conn->seq);
	header->ack			   = HOST2BE_DWORD(conn->ack);
	header->flags		   = TCP_FLAG_PSH | TCP_FLAG_ACK;
	header->ack			   = HOST2BE_DWORD(conn->ack);
	header->window		   = HOST2BE_WORD(conn->rwin.size - conn->rwin.acked - conn->rwin.recved);
	header->urgent_pointer = 0;

	memcpy(sendbuf + sizeof(tcp_header_t), buf, length);
	header->checksum = 0;
	header->checksum = HOST2BE_WORD(tcp_checksum(netc, conn->dst_ip, sendbuf, sizeof(tcp_header_t) + length));

	conn->swin.unacked += length;
	conn->swin.sended -= length;

	netc_ip_send(netc, conn->dst_ip, 0, PROTOCOL_TCP, sendbuf, sizeof(tcp_header_t) + length);

	conn->wait_ms = 600;
	conn->timer	  = timer_alloc();
	timer_init(conn->timer, &conn->fifo, 0);
	fifo_init(&conn->fifo, 2, conn->fifo_buf);
	timer_settime(conn->timer, conn->wait_ms / 10);
	while (conn->swin.unacked) {
		if (fifo_status(&conn->fifo)) {
			while (fifo_status(&conn->fifo))
				fifo_get(&conn->fifo);
			conn->wait_ms *= 2;
			if (conn->wait_ms >= 120000) { return -1; }
			netc_ip_send(netc, conn->dst_ip, 0, PROTOCOL_TCP, sendbuf, sizeof(tcp_header_t) + length);
			timer_settime(conn->timer, conn->wait_ms / 10);
		}
	}
	kfree(sendbuf);
	timer_free(conn->timer);
	if (fifo_status(&conn->fifo)) fifo_get(&conn->fifo);
	return 0;
}

int wait_for_status_change(netc_t *netc) {
	tcp_conn_t  *conn	= (tcp_conn_t *)netc->app_private;
	tcp_status_t status = conn->status;
	int			 i		= 0;

	conn->wait_ms = 1000;
	conn->timer	  = timer_alloc();
	timer_init(conn->timer, &conn->fifo, 0);
	fifo_init(&conn->fifo, 2, conn->fifo_buf);
	timer_settime(conn->timer, conn->wait_ms / 10);
	while (conn->status == status) {
		if (fifo_status(&conn->fifo)) {
			while (fifo_status(&conn->fifo))
				fifo_get(&conn->fifo);
			i++;
			if (i > 10) return -1;
		}
	}
	timer_free(conn->timer);
	if (fifo_status(&conn->fifo)) fifo_get(&conn->fifo);
	return 0;
}

void tcp_analyse_option(netc_t *netc, uint8_t *buf) {
	int			i			= 0;
	tcp_conn_t *conn		= (tcp_conn_t *)netc->app_private;
	uint8_t		option_size = (((tcp_header_t *)buf)->offset >> 4) * 4 - sizeof(tcp_header_t);
	buf += sizeof(tcp_header_t);

	while (i < option_size) {
		switch (buf[i]) {
		case TCP_OPTION_END:
			return;
		case TCP_OPTION_NOP:
			break;
		case TCP_OPTION_MSS:
			conn->mss = MIN(conn->mss, BE2HOST_WORD(*(uint16_t *)(buf + i + 2)));
			break;
		default:
			break;
		}
	}
}

void tcp_create(netc_t *netc) {
	tcp_conn_t *conn;
	netc->app_private = kmalloc(sizeof(tcp_conn_t));
	conn			  = (tcp_conn_t *)netc->app_private;
	conn->netc		  = netc;
}

int tcp_bind(netc_t *netc, uint16_t src_port) {
	tcp_conn_t *conn = (tcp_conn_t *)netc->app_private, *cur, *next;

	spin_lock(&tcp_lock);
	list_for_each_owner_safe (cur, next, &tcp_lh, list) {
		if (cur->src_port == src_port) { return -1; }
	}
	list_add_tail(&conn->list, &tcp_lh);
	spin_unlock(&tcp_lock);

	conn->src_port = src_port;
	conn->seq	   = rand();
	conn->ack	   = 0;

	conn->rwin.mem = kmalloc(rmem_defualt_size);

	conn->rwin.size	   = rmem_defualt_size;
	conn->swin.size	   = 0;
	conn->rwin.acked   = 0;
	conn->rwin.unacked = 0;
	conn->swin.unacked = 0;
	conn->rwin.recved  = 0;
	conn->swin.sended  = 0;
	conn->rwin.head	   = 0;
	conn->swin.head	   = 0;
	conn->rwin.tail	   = 0;
	conn->swin.tail	   = 0;

	conn->mss = netc_get_mtu(netc) - sizeof(tcp_header_t);

	conn->status = TCP_STAT_CLOSED;
	return 0;
}

int tcp_unbind(netc_t *netc) {
	tcp_conn_t *conn = (tcp_conn_t *)netc->app_private;
	spin_lock(&tcp_lock);
	list_del(&conn->list);
	list_add_tail(&conn->list, &tcp_lh);
	return 0;
}

int tcp_ipv4_connect(netc_t *netc, uint8_t *ip, uint16_t dst_port) {
	tcp_header_t *header;
	tcp_conn_t   *conn;
	int			  tcp_length = sizeof(tcp_header_t) + 4;

	conn			 = (tcp_conn_t *)netc->app_private;
	conn->header_buf = kmalloc(sizeof(tcp_header_t));
	header			 = (tcp_header_t *)conn->header_buf;

	conn->dst_port = dst_port;
	memcpy(conn->dst_ip, ip, 4);

	// TCP第一次握手

	header->src_port	   = HOST2BE_WORD(conn->src_port);
	header->dst_port	   = HOST2BE_WORD(conn->dst_port);
	header->seq			   = HOST2BE_DWORD(conn->seq);
	header->ack			   = 0;
	header->offset		   = (tcp_length / 4) << 4;
	header->flags		   = TCP_FLAG_SYN;
	header->window		   = HOST2BE_WORD(rmem_defualt_size);
	header->urgent_pointer = 0;
	header->checksum	   = 0;

	// 设置TCP Option最大分片大小(Maximum segment size)
	conn->header_buf[sizeof(tcp_header_t) + 0]				   = TCP_OPTION_MSS;
	conn->header_buf[sizeof(tcp_header_t) + 1]				   = 4; // 该Option长度为4字节
	*(uint16_t *)(conn->header_buf + sizeof(tcp_header_t) + 2) = HOST2BE_WORD(1460);

	header->checksum = HOST2BE_WORD(tcp_checksum(netc, conn->dst_ip, conn->header_buf, tcp_length));

	conn->status = TCP_STAT_SYN_SENT;

	conn->seq += 1;
	tcp_send_for_status_change(netc, conn->header_buf, tcp_length); // 发送SYN包
	tcp_length	   = sizeof(tcp_header_t);
	header->offset = (tcp_length / 4) << 4;
	if (conn->status == TCP_STAT_CLOSED) {
		kfree(conn->rwin.mem);
		timer_free(conn->timer);
		kfree(conn);
		return -1;
	}

	conn->swin.mem = (uint8_t *)kmalloc(conn->swin.size);

	// 第二次握手

	// 回应ACK
	header->seq		 = HOST2BE_DWORD(conn->seq);
	header->ack		 = HOST2BE_DWORD(conn->ack);
	header->flags	 = TCP_FLAG_ACK;
	header->checksum = 0;
	header->checksum = HOST2BE_WORD(tcp_checksum(netc, conn->dst_ip, conn->header_buf, tcp_length));
	netc_ip_send(netc, conn->dst_ip, 0, PROTOCOL_TCP, conn->header_buf, tcp_length);

	return 0;
}

int tcp_listen(netc_t *netc) {
	tcp_header_t *header;
	tcp_conn_t   *conn;
	int			  tcp_length = sizeof(tcp_header_t);

	conn = (tcp_conn_t *)netc->app_private;

	conn->status = TCP_STAT_LISTEN;
	if (wait_for_status_change(netc) != 0) return -1;

	conn->header_buf	   = kmalloc(tcp_length);
	header				   = (tcp_header_t *)conn->header_buf;
	header->src_port	   = HOST2BE_WORD(conn->src_port);
	header->dst_port	   = HOST2BE_WORD(conn->dst_port);
	header->seq			   = HOST2BE_DWORD(conn->seq);
	header->ack			   = HOST2BE_DWORD(conn->ack);
	header->offset		   = (tcp_length / 4) << 4;
	header->flags		   = TCP_FLAG_SYN | TCP_FLAG_ACK;
	header->window		   = HOST2BE_WORD(rmem_defualt_size);
	header->urgent_pointer = 0;
	header->checksum	   = 0;
	header->checksum	   = HOST2BE_WORD(tcp_checksum(netc, conn->dst_ip, conn->header_buf, tcp_length));

	conn->seq += 1;
	tcp_send_for_status_change(netc, conn->header_buf, tcp_length);
	if (conn->status != TCP_STAT_ESTABLISHED) return -2;
	return 0;
}

void tcp_ipv4_close(netc_t *netc) {
	tcp_header_t *header;
	tcp_conn_t   *conn = (tcp_conn_t *)netc->app_private;
	header			   = (tcp_header_t *)conn->header_buf;

	header->seq			   = HOST2BE_DWORD(conn->seq);
	header->ack			   = HOST2BE_DWORD(conn->ack);
	header->flags		   = TCP_FLAG_FIN | TCP_FLAG_ACK;
	header->window		   = HOST2BE_WORD(conn->rwin.size - conn->rwin.recved - conn->rwin.acked);
	header->urgent_pointer = 0;
	header->checksum	   = 0;
	header->checksum = HOST2BE_WORD(tcp_checksum(netc, conn->dst_ip, conn->header_buf, sizeof(tcp_header_t)));

	conn->status = TCP_STAT_FIN_WAIT1;
	conn->seq += 1;
	tcp_send_for_status_change(netc, conn->header_buf, sizeof(tcp_header_t));

	if (conn->status == TCP_STAT_FIN_WAIT2) {
		if (wait_for_status_change(netc) != 0) goto end;
	}
	if (conn->status == TCP_STAT_TIME_WAIT) {
		header->seq		 = HOST2BE_DWORD(conn->seq);
		header->ack		 = HOST2BE_DWORD(conn->ack);
		header->flags	 = TCP_FLAG_ACK;
		header->checksum = 0;
		header->checksum =
			HOST2BE_WORD(tcp_checksum(netc, conn->dst_ip, conn->header_buf, sizeof(tcp_header_t)));

		netc_ip_send(netc, conn->dst_ip, 0, PROTOCOL_TCP, conn->header_buf, sizeof(tcp_header_t));

		conn->wait_ms = 2 * TCP_MSL_MS;
		timer_settime(conn->timer, conn->wait_ms / 10);
		while (!fifo_status(&conn->fifo))
			;
		fifo_get(&conn->fifo);
	}
end:
	kfree(conn->rwin.mem);
	timer_free(conn->timer);
	kfree(conn);
	return;
}

void tcp_recv(uint8_t *buf, uint16_t offset, uint16_t length, uint8_t *ip, uint8_t ip_len) {
	netc_t	   *netc;
	tcp_header_t *tcp_head = (tcp_header_t *)(buf + offset);
	uint16_t	  chksum   = tcp_head->checksum;
	tcp_header_t *header;
	int			  flag = 1;
	int			  len, head_len;

	uint16_t src_port = BE2HOST_WORD(tcp_head->src_port);
	uint16_t dst_port = BE2HOST_WORD(tcp_head->dst_port);

	tcp_conn_t *conn, *cur, *next;
	spin_lock(&tcp_lock);
	list_for_each_owner_safe (cur, next, &tcp_lh, list) {
		if (cur->src_port == dst_port) {
			if ((cur->dst_port == src_port && memcmp(cur->dst_ip, ip, ip_len) == 0) ||
				cur->status == TCP_STAT_LISTEN) {
				conn = cur;
				flag = 0;
				break;
			}
		}
	}
	spin_unlock(&tcp_lock);

	if (flag || conn->status == TCP_STAT_CLOSED) { return; }
	netc = conn->netc;

	tcp_head->checksum = 0;
	if (chksum != HOST2BE_WORD(tcp_checksum(netc, netc->dst_laddr, buf + offset, length))) { return; }
	if (tcp_head->flags & TCP_FLAG_RST) { conn->status = TCP_STAT_CLOSED; }

	header	 = (tcp_header_t *)conn->header_buf;
	head_len = (tcp_head->offset & 0xf0) >> 2;
	len		 = length - head_len;

	uint32_t r_seq = BE2HOST_DWORD(tcp_head->seq);
	uint32_t r_ack = BE2HOST_DWORD(tcp_head->ack);

	switch (conn->status) {
	case TCP_STAT_LISTEN:
		if (tcp_head->flags & TCP_FLAG_SYN) {
			conn->ack	   = r_seq;
			conn->dst_port = BE2HOST_WORD(tcp_head->dst_port);
			memcpy(conn->dst_ip, ip, ip_len);
			conn->status = TCP_STAT_SYN_RCVD;
		}
		break;
	case TCP_STAT_SYN_SENT:
		if (tcp_head->flags & TCP_FLAG_SYN) {
			conn->swin.size = MIN(wmem_defualt_size, BE2HOST_WORD(tcp_head->window));
			conn->ack		= r_seq;
		}
		if (tcp_head->flags & TCP_FLAG_ACK) {
			if (conn->seq == r_ack) { conn->status = TCP_STAT_ESTABLISHED; }
		}
		len = 1;
		break;
	case TCP_STAT_SYN_RCVD:
		if (tcp_head->flags & TCP_FLAG_ACK) {
			if (r_ack == conn->seq) {
				conn->ack	 = r_seq;
				conn->status = TCP_STAT_ESTABLISHED;
			}
		}
		len = 1;
		break;
	case TCP_STAT_ESTABLISHED:
		// printk("TCP seq=%04X, ack=%04X\n", r_seq, r_ack);
		if (len > 0) {
			if (conn->rwin.recved + len < conn->rwin.size) {
				memcpy(conn->rwin.mem + conn->rwin.head + conn->rwin.acked + conn->rwin.recved,
					   buf + offset + head_len, len);
			} else {
				int tmp = conn->rwin.size - conn->rwin.recved;
				memcpy(conn->rwin.mem + conn->rwin.head + conn->rwin.acked + conn->rwin.recved,
					   buf + offset + head_len, tmp);
				memcpy(conn->rwin.mem, buf + offset + head_len + tmp, len - tmp);
			}
			conn->rwin.recved += len;
		}
		// 先处理ACK信息
		uint32_t add = 0;
		if (tcp_head->flags & TCP_FLAG_ACK) {
			add = len;
			if (conn->seq < r_ack) { // 对方确认己方发送的数据
				uint32_t tmp = r_ack - conn->seq;
				conn->swin.unacked -= tmp;
				conn->seq += tmp;
				conn->swin.head = (conn->swin.head + tmp) % conn->swin.size;
				conn->swin.tail = (conn->swin.tail + tmp) % conn->swin.size;
			}
			if ((add > 0 && list_empty(&net_rx_tcp_lh)) || tcp_head->flags & TCP_FLAG_PSH) {
				header->seq			   = HOST2BE_DWORD(conn->seq);
				header->ack			   = HOST2BE_DWORD(conn->ack + add);
				header->flags		   = TCP_FLAG_ACK;
				header->window		   = HOST2BE_WORD(conn->rwin.size - conn->rwin.recved);
				header->urgent_pointer = 0;
				header->checksum	   = 0;
				header->checksum =
					HOST2BE_WORD(tcp_checksum(netc, conn->dst_ip, conn->header_buf, sizeof(tcp_header_t)));
				conn->swin.head = (conn->swin.head + conn->rwin.unacked + add) % conn->swin.size;
				conn->swin.tail = (conn->swin.tail + conn->rwin.unacked + add) % conn->swin.size;
				// 发送ACK包表示确认收到
				conn->rwin.acked += conn->rwin.unacked + add;
				conn->rwin.recved -= conn->rwin.unacked + add;
				conn->rwin.unacked = 0;
				netc_ip_send(netc, conn->dst_ip, 0, PROTOCOL_TCP, conn->header_buf, sizeof(tcp_header_t));
			} else {
				conn->rwin.unacked += add;
			}
		}
		if (tcp_head->flags & TCP_FLAG_FIN) {
			header->seq			   = HOST2BE_DWORD(conn->seq);
			header->ack			   = HOST2BE_DWORD(conn->ack + len);
			header->flags		   = TCP_FLAG_ACK;
			header->window		   = HOST2BE_WORD(conn->rwin.size - conn->rwin.recved);
			header->urgent_pointer = 0;
			header->checksum	   = 0;
			header->flags |= TCP_FLAG_FIN;
			header->checksum =
				HOST2BE_WORD(tcp_checksum(netc, conn->dst_ip, conn->header_buf, sizeof(tcp_header_t)));
			netc_ip_send(netc, conn->dst_ip, 0, PROTOCOL_TCP, conn->header_buf, sizeof(tcp_header_t));
			if (conn->swin.unacked == 0) {
				conn->status = TCP_STAT_LAST_ACK;
			} else {
				conn->status = TCP_STAT_CLOSE_WAIT;
			}
		}
		break;
	case TCP_STAT_FIN_WAIT1:
		if (tcp_head->flags & TCP_FLAG_ACK) {
			if (conn->seq == r_ack) {
				conn->status = TCP_STAT_FIN_WAIT2;
				kfree(conn->swin.mem);
				conn->ack = r_seq;
				if (tcp_head->flags & TCP_FLAG_FIN) { conn->status = TCP_STAT_TIME_WAIT; }
			}
		}
		break;
	case TCP_STAT_FIN_WAIT2:
		if (tcp_head->flags & TCP_FLAG_FIN) { conn->status = TCP_STAT_TIME_WAIT; }
		if (len == 0) { len = 1; }
		break;
	case TCP_STAT_LAST_ACK:
		if (tcp_head->flags & TCP_FLAG_ACK) { conn->status = TCP_STAT_CLOSED; }
		break;

	default:
		break;
	}
	conn->ack += len;
}

int tcp_write(netc_t *netc, uint8_t *buf, uint32_t length) {
	tcp_conn_t *conn = (tcp_conn_t *)netc->app_private;
	int			tmp;

	tmp = conn->swin.size - conn->swin.unacked - conn->swin.sended;

	do {
		tmp = MIN(MIN(length, tmp), conn->mss);
		memcpy(conn->swin.mem + conn->swin.sended, buf, tmp);
		length -= tmp;
		buf += tmp;
		conn->swin.sended = (conn->swin.sended + tmp) % conn->swin.size;
		tcp_send(netc, conn->swin.mem + conn->swin.unacked, tmp);
	} while (length > 0);
	return 0;
}

/**
 * @brief 读取TCP数据
 *
 * @param netc
 * @param buf 数据缓冲区
 * @param length 读取的数据长度
 * @return int 实际读取的数据长度
 */
int tcp_read(netc_t *netc, uint8_t *buf, uint32_t length) {
	tcp_conn_t *conn = (tcp_conn_t *)netc->app_private;
	int			i	 = 0;
	while (conn->rwin.acked == 0) {
		i++;
		delay(1);
		if (i == 1000) { break; }
	}

	/**
	 * @brief 同时增加head和tail，减小acked，释放部分空间
	 *
	 */
	if (conn->rwin.acked > 0) {
		int size = MIN(conn->rwin.acked, length);
		memcpy(buf, conn->rwin.mem + conn->rwin.head, size);
		conn->rwin.head = (conn->rwin.head + size) % conn->rwin.size;
		conn->rwin.tail = (conn->rwin.tail + size) % conn->rwin.size;
		conn->rwin.acked -= size;
		return size;
	}
	return 0;
}
