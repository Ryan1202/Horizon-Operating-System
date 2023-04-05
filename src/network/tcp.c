#include <drivers/pit.h>
#include <kernel/func.h>
#include <math.h>
#include <network/ipv4.h>
#include <network/network.h>
#include <network/tcp.h>
#include <stdlib.h>

SPINLOCK(tcp_lock);
LIST_HEAD(tcp_lh);

const int rmem_defualt_size = 32768;
const int wmem_defualt_size = 16384;

int tcp_send_for_status_change(netc_t *netc, uint8_t *buf, int length) {
	tcp_conn_t	*conn	= (tcp_conn_t *)netc->app_private;
	tcp_status_t status = conn->status;

	ipv4_send(netc, conn->dst_ip, 0, 64, PROTOCOL_TCP, buf, length);

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
			ipv4_send(netc, conn->dst_ip, 0, 64, PROTOCOL_TCP, buf, length);
			timer_settime(conn->timer, conn->wait_ms / 10);
		}
	}
	timer_free(conn->timer);
	if (fifo_status(&conn->fifo)) fifo_get(&conn->fifo);
	return 0;
}

int wait_for_status_change(netc_t *netc) {
	tcp_conn_t	*conn	= (tcp_conn_t *)netc->app_private;
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
	chksum += ((tcp_header_t *)buf)->offset >> 2;
	for (i = 0; i < length / sizeof(uint16_t); i++) {
		chksum += (buf[i * 2] << 8) + buf[i * 2 + 1];
	}
	if (length % sizeof(uint16_t)) { chksum += buf[length - 1] << 8; }
	while (chksum & 0xffff0000) {
		chksum = (chksum >> 16) + (chksum & 0xffff);
	}

	return (~chksum) & 0xffff;
}

void tcp_create(netc_t *netc) {
	tcp_conn_t *conn;
	netc->app_private = kmalloc(sizeof(tcp_conn_t));
	conn			  = (tcp_conn_t *)netc->app_private;
	conn->netc		  = netc;
}

int tcp_bind(netc_t *netc, uint8_t ip[4], uint16_t src_port, uint16_t dst_port) {
	tcp_conn_t *conn = (tcp_conn_t *)netc->app_private, *cur, *next;

	spin_lock(&tcp_lock);
	list_for_each_owner_safe (cur, next, &tcp_lh, list) {
		if (cur->src_port == src_port) { return -1; }
	}
	list_add_tail(&conn->list, &tcp_lh);
	spin_unlock(&tcp_lock);

	conn->src_port = src_port;
	conn->dst_port = dst_port;
	memcpy(conn->dst_ip, ip, 4);

	conn->rwin.mem = kmalloc(rmem_defualt_size);

	conn->rwin.size		 = rmem_defualt_size;
	conn->wwin.size		 = 0;
	conn->rwin.idx_write = conn->wwin.idx_write = 0;
	conn->rwin.idx_ack = conn->wwin.idx_ack = 0;
	conn->wwin.idx_send						= 0;
	return 0;
}

int tcp_unbind(netc_t *netc) {
	tcp_conn_t *conn = (tcp_conn_t *)netc->app_private;
	spin_lock(&tcp_lock);
	list_del(&conn->list);
	list_add_tail(&conn->list, &tcp_lh);
	return 0;
}

int tcp_ipv4_connect(netc_t *netc) {
	tcp_header_t *header;
	tcp_conn_t	 *conn;
	uint8_t		 *buf;
	int			  tcp_length = sizeof(tcp_header_t);

	conn   = (tcp_conn_t *)netc->app_private;
	buf	   = kmalloc(tcp_length);
	header = (tcp_header_t *)buf;

	header->src_port	   = HOST2BE_WORD(conn->src_port);
	header->dst_port	   = HOST2BE_WORD(conn->dst_port);
	conn->seq			   = rand();
	conn->ack			   = 0;
	header->seq			   = HOST2BE_DWORD(conn->seq);
	header->ack			   = 0;
	header->offset		   = (tcp_length / 4) << 4;
	header->flags		   = TCP_FLAG_SYN;
	header->window		   = HOST2BE_WORD(rmem_defualt_size);
	header->urgent_pointer = 0;
	header->checksum	   = 0;

	header->checksum = HOST2BE_WORD(tcp_checksum(netc, conn->dst_ip, buf, tcp_length));

	conn->status = TCP_STAT_SYN_SENT;

	tcp_send_for_status_change(netc, buf, tcp_length);
	if (conn->status == TCP_STAT_CLOSED) {
		kfree(conn->rwin.mem);
		timer_free(conn->timer);
		kfree(conn);
		return -1;
	}

	conn->seq += 1;
	header->seq		 = HOST2BE_DWORD(conn->seq);
	header->ack		 = HOST2BE_DWORD(conn->ack);
	header->flags	 = TCP_FLAG_ACK;
	header->checksum = 0;
	header->checksum = HOST2BE_WORD(tcp_checksum(netc, conn->dst_ip, buf, tcp_length));
	ipv4_send(netc, conn->dst_ip, 0, 64, PROTOCOL_TCP, buf, tcp_length);

	return 0;
}

void tcp_ipv4_close(netc_t *netc) {
	uint8_t		 *buf	 = kmalloc(sizeof(tcp_header_t));
	tcp_header_t *header = (tcp_header_t *)buf;
	tcp_conn_t	 *conn	 = (tcp_conn_t *)netc->app_private;

	header->src_port	   = HOST2BE_WORD(conn->src_port);
	header->dst_port	   = HOST2BE_WORD(conn->dst_port);
	header->seq			   = HOST2BE_DWORD(conn->seq);
	header->ack			   = HOST2BE_DWORD(conn->ack);
	header->offset		   = (sizeof(tcp_header_t) / 4) << 4;
	header->flags		   = TCP_FLAG_FIN | TCP_FLAG_ACK;
	header->window		   = HOST2BE_WORD(conn->rwin.size - conn->rwin.idx_write);
	header->urgent_pointer = 0;
	header->checksum	   = 0;
	header->checksum	   = HOST2BE_WORD(tcp_checksum(netc, conn->dst_ip, buf, sizeof(tcp_header_t)));

	conn->status = TCP_STAT_FIN_WAIT1;
	tcp_send_for_status_change(netc, buf, sizeof(tcp_header_t));

	if (conn->status == TCP_STAT_FIN_WAIT2) {
		if (wait_for_status_change(netc) != 0) goto end;
	}
	if (conn->status == TCP_STAT_TIME_WAIT) {
		conn->seq += 1;
		header->seq		 = HOST2BE_DWORD(conn->seq);
		header->ack		 = HOST2BE_DWORD(conn->ack);
		header->flags	 = TCP_FLAG_ACK;
		header->checksum = 0;
		header->checksum = HOST2BE_WORD(tcp_checksum(netc, conn->dst_ip, buf, sizeof(tcp_header_t)));

		ipv4_send(netc, conn->dst_ip, 0, 64, PROTOCOL_TCP, buf, sizeof(tcp_header_t));

		conn->wait_ms = 2 * TCP_MSL_MS;
		timer_settime(conn->timer, conn->wait_ms);
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

void tcp_read(uint8_t *buf, uint16_t offset, uint16_t length) {
	netc_t		 *netc;
	tcp_header_t *tcp_head = (tcp_header_t *)(buf + offset);
	uint16_t	  chksum   = tcp_head->checksum;
	int			  flag	   = 1;

	uint16_t src_port = BE2HOST_WORD(tcp_head->src_port);
	uint16_t dst_port = BE2HOST_WORD(tcp_head->dst_port);

	tcp_conn_t *conn, *cur, *next;
	spin_lock(&tcp_lock);
	list_for_each_owner_safe (cur, next, &tcp_lh, list) {
		if (cur->dst_port == src_port && cur->src_port == dst_port) {
			conn = cur;
			flag = 0;
			break;
		}
	}
	spin_unlock(&tcp_lock);

	if (flag) { return; }
	netc = conn->netc;

	tcp_head->checksum = 0;
	if (chksum != HOST2BE_WORD(tcp_checksum(netc, netc->dst_laddr, buf + offset, length))) { return; }

	if (tcp_head->flags & TCP_FLAG_RST) { conn->status = TCP_STAT_CLOSED; }
	switch (conn->status) {
	case TCP_STAT_SYN_SENT:
		if (tcp_head->flags & TCP_FLAG_SYN) {
			conn->wwin.size = MIN(wmem_defualt_size, BE2HOST_WORD(tcp_head->window));
			conn->wwin.mem	= kmalloc(conn->wwin.size);
		}
		if (tcp_head->flags & TCP_FLAG_ACK) {
			if (conn->seq + 1 == BE2HOST_DWORD(tcp_head->ack)) {
				conn->ack	 = BE2HOST_DWORD(tcp_head->seq) + 1;
				conn->status = TCP_STAT_ESTABLISHED;
			}
		}
		break;
	case TCP_STAT_FIN_WAIT1:
		if (tcp_head->flags & TCP_FLAG_ACK) {
			if (conn->seq + 1 == BE2HOST_DWORD(tcp_head->ack)) {
				conn->status = TCP_STAT_FIN_WAIT2;
				kfree(conn->wwin.mem);
				conn->ack = BE2HOST_DWORD(tcp_head->seq) + 1;
				if (tcp_head->flags & TCP_FLAG_FIN) { conn->status = TCP_STAT_TIME_WAIT; }
			}
		}
		break;
	case TCP_STAT_FIN_WAIT2:
		if (tcp_head->flags & TCP_FLAG_FIN) { conn->status = TCP_STAT_TIME_WAIT; }
		break;

	default:
		break;
	}
}