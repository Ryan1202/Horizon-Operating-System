#include <bits.h>
#include <kernel/driver.h>
#include <kernel/memory.h>
#include <network/ipv4.h>
#include <network/network.h>
#include <network/udp.h>
#include <stdint.h>

SPINLOCK(udp_lock);
LIST_HEAD(udp_lh);

int udp_bind(netc_t *netc, uint8_t ip[4], uint16_t src_port, uint16_t dst_port) {
	udp_conn_t *conn, *cur, *next;
	netc->app_private = kmalloc(sizeof(udp_conn_t));

	conn	   = (udp_conn_t *)netc->app_private;
	conn->netc = netc;

	spin_lock(&udp_lock);
	list_for_each_owner_safe (cur, next, &udp_lh, list) {
		if (cur->src_port == src_port) { return -1; }
	}
	list_add_tail(&conn->list, &udp_lh);
	spin_unlock(&udp_lock);

	conn->src_port = src_port;
	conn->dst_port = dst_port;
	memcpy(conn->dst_ip, ip, 4);
	return 0;
}

int udp_unbind(netc_t *netc) {
	udp_conn_t *conn = (udp_conn_t *)netc->app_private;
	spin_lock(&udp_lock);
	list_del(&conn->list);
	list_add_tail(&conn->list, &udp_lh);
	return 0;
}

void udp_send(netc_t *netc, uint16_t *data, uint16_t datalen) {
	uint32_t	chksum = 0, i;
	udp_head_t *udp_head;
	udp_conn_t *conn = (udp_conn_t *)netc->app_private;
	uint16_t   *buf	 = kmalloc(sizeof(udp_head_t) + datalen);
	uint8_t		src_ip[4];
	udp_head = (udp_head_t *)buf;
	ipv4_get_ip(netc, src_ip);

	udp_head->SourcePort	  = HOST2BE_WORD(conn->src_port);
	udp_head->DestinationPort = HOST2BE_WORD(conn->dst_port);
	udp_head->Length		  = HOST2BE_WORD(sizeof(udp_head_t) + datalen);
	udp_head->Checksum		  = 0; // 为了方便计算，先置0
	memcpy((uint8_t *)buf + sizeof(udp_head_t), data, datalen);

	// 计算校验和
	// UDP伪首部
	for (i = 0; i < 4 / sizeof(uint16_t); i++) {
		chksum += (src_ip[i * 2] << 8) + src_ip[i * 2 + 1];
		chksum += (conn->dst_ip[i * 2] << 8) + conn->dst_ip[i * 2 + 1];
	}
	chksum += PROTOCOL_UDP;
	chksum += HOST2BE_WORD(udp_head->Length);
	// UDP首部
	for (i = 0; i < (sizeof(udp_head_t) + datalen) / sizeof(uint16_t); i++) {
		chksum += BE2HOST_WORD(buf[i]);
	}
	udp_head->Checksum = HOST2BE_WORD(~(uint16_t)((chksum & 0xffff) + (chksum >> 16)));

	ipv4_send(netc, conn->dst_ip, 0, 64, PROTOCOL_UDP, (uint8_t *)buf, sizeof(udp_head_t) + datalen);
	kfree(buf);
}

void udp_read(uint8_t *buf, uint16_t offset, uint16_t length) {
	netc_t	   *netc;
	udp_head_t *udp_head = (udp_head_t *)(buf + offset);
	udp_conn_t *conn, *cur, *next;
	int			flag = 1;

	udp_head->DestinationPort = BE2HOST_WORD(udp_head->DestinationPort);
	udp_head->SourcePort	  = BE2HOST_WORD(udp_head->SourcePort);

	spin_lock(&udp_lock);
	list_for_each_owner_safe (cur, next, &udp_lh, list) {
		if (cur->dst_port == udp_head->SourcePort && cur->src_port == udp_head->DestinationPort) {
			conn = cur;
			flag = 0;
			break;
		}
	}
	spin_unlock(&udp_lock);
	if (flag) { return; }
	netc = conn->netc;

	uint32_t off = (netc->recv_offset + netc->recv_len) % NET_MAX_BUFFER_SIZE;
	if (off + length - sizeof(udp_head_t) > NET_MAX_BUFFER_SIZE) {
		int tmp = NET_MAX_BUFFER_SIZE - netc->recv_offset;
		memcpy(netc->recv_buffer + off, buf + offset + sizeof(udp_head_t), tmp);
		memcpy(netc->recv_buffer, buf + offset + sizeof(udp_head_t) + tmp, length - sizeof(udp_head_t) - tmp);
	} else {
		memcpy(netc->recv_buffer + off, buf + offset + sizeof(udp_head_t), length - sizeof(udp_head_t));
	}
	netc->recv_len += length - sizeof(udp_head_t);
}