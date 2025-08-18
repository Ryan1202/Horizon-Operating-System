/**
 * @file udp.c
 * @author Jiajun Wang (ryan1202@foxmail.com)
 * @brief UDP
 *
 * Reference:
 * RFC 768: User Datagram Protocol
 *
 */
#include "bits.h"
#include "driver/network/buffer.h"
#include "driver/network/protocols/protocols.h"
#include "kernel/memory.h"
#include "kernel/spinlock.h"
#include "kernel/thread.h"
#include <driver/network/conn.h>
#include <driver/network/protocols/ipv4.h>
#include <driver/network/protocols/udp.h>
#include <kernel/list.h>
#include <stdint.h>
#include <string.h>

#define UDP_CHECKSUM_ADD(sum, data) \
	sum += (data);                  \
	if (sum > 0xFFFF) sum = (sum & 0xFFFF) + (sum >> 16);

LIST_HEAD(udp_lh);

uint16_t udp_checksum(
	NetBuffer *net_buffer, void *ip[2], int ip_len, UdpHeader *udp_header) {
	uint16_t *data = (uint16_t *)udp_header;
	uint32_t  sum  = 0;
	uint16_t  len  = net_buffer->tail - net_buffer->data;

	// 清除校验和
	udp_header->checksum = 0;

	// 计算伪头部校验和
	for (int j = 0; j < ip_len / 2; j++) {
		UDP_CHECKSUM_ADD(sum, ((uint16_t *)ip[0])[j]);
	}
	for (int j = 0; j < ip_len / 2; j++) {
		UDP_CHECKSUM_ADD(sum, ((uint16_t *)ip[1])[j]);
	}
	UDP_CHECKSUM_ADD(sum, HOST2BE_WORD(IP_PROTO_UDP));
	UDP_CHECKSUM_ADD(sum, udp_header->length);

	// 计算UDP头部和数据的校验和
	for (size_t i = 0; i < len / 2; i++) {
		UDP_CHECKSUM_ADD(sum, data[i]);
	}

	if (len % 2) { // 如果长度是奇数，补齐一个字节
		sum += ((uint8_t *)data)[len - 1];
	}

	return ~((uint16_t)(sum + (sum >> 16)));
}

void udp_register(NetworkConnection *conn) {
	conn->trans_protocol = TRANS_PROTO_UDP;
	NET_BUF_RESV_HEAD(conn, sizeof(UdpHeader));
}

void udp_enqueue_packet(NetworkConnection *conn, NetBuffer *net_buffer) {
	disable_preempt();
	spin_lock(&conn->recv_lock);

	list_add_tail(&net_buffer->list, &conn->recv_lh);

	spin_unlock(&conn->recv_lock);
	thread_unblock(conn->thread);
	enable_preempt();
}

/*
 * 设置UDP回调函数
 */
void udp_set_callback(
	NetworkConnection *conn,
	void (*callback)(NetworkConnection *conn, NetBuffer *buffer)) {
	if (conn != NULL && conn->trans_protocol == TRANS_PROTO_UDP &&
		callback != NULL) {
		conn->udp.callback = callback;
	} else {
		printk("[UDP] Invalid connection or callback function\n");
	}
}

void udp_wrap(NetworkConnection *conn, uint16_t src_port, uint16_t dst_port) {
	uint16_t size = CONN_CONTENT_SIZE(conn);
	conn_header_alloc(conn, sizeof(UdpHeader));
	UdpHeader *udp_header = (UdpHeader *)conn->buffer->head;
	udp_header->src_port  = HOST2BE_WORD(src_port);
	udp_header->dst_port  = HOST2BE_WORD(dst_port);
	udp_header->length	  = HOST2BE_WORD(sizeof(UdpHeader) + size);
	udp_header->checksum  = 0; // 暂时不计算校验和
}

void udp_bind(NetworkConnection *conn, uint16_t port) {
	Ipv4ConnInfo *conn_info = &conn->ipv4.conn_info;
	if (list_in_list(&conn_info->list)) return; // 已经绑定了端口
	conn_info->local.port = port;
	list_add_tail(&conn_info->list, &udp_lh);
}

void udp_unbind(NetworkConnection *conn) {
	Ipv4ConnInfo *conn_info = &conn->ipv4.conn_info;
	if (!list_in_list(&conn_info->list)) return; // 没有绑定端口
	list_del(&conn_info->list);
}

ProtocolResult udp_recv(NetBuffer *net_buffer, Ipv4Header *ipv4_header) {
	UdpHeader *udp_header = (UdpHeader *)net_buffer->data;
	int		   size		  = net_buffer->tail - net_buffer->data;

	int length = BE2HOST_WORD(udp_header->length);
	if (length < sizeof(UdpHeader)) return PROTO_ERROR_UNSUPPORT; // 数据包太小
	if (length > size) return PROTO_ERROR_EXCEED_MAX_SIZE; // 长度不合法

	// TODO: 先检查目标是否为本机IP

	// 计算校验和（如果需要）
	if (udp_header->checksum != 0) {
		uint16_t checksum = udp_header->checksum;
		void	*ip[2]	  = {
			  ipv4_header->src_ip, // 源IP地址
			  ipv4_header->dst_ip, // 目的IP地址
		  };

		uint16_t calc_checksum = udp_checksum(net_buffer, ip, 4, udp_header);
		if (calc_checksum != checksum) { return PROTO_ERROR_CHECKSUM; }
	}

	net_buffer->data += sizeof(UdpHeader);

	// IPv4
	Ipv4ConnInfo *info, *next;
	if (list_empty(&udp_lh)) {
		// 没有绑定的连接
		goto drop;
	}
	list_for_each_owner_safe (info, next, &udp_lh, list) {
		if (info->local.port == BE2HOST_WORD(udp_header->dst_port) &&
			(info->remote.port == 0 ||
			 info->remote.port == BE2HOST_WORD(udp_header->src_port))) {
			if (memcmp(info->local.ip, ipv4_header->dst_ip, 4)) {
				if (memcmp(
						info->local.ip, (void *)&ipv4_null_addr,
						4) || // 不是发送到0.0.0.0
					memcmp(
						ipv4_header->dst_ip, (void *)&ipv4_broadcast_addr,
						4)) // 也不是广播
					continue;
			}

			// 找到匹配的连接
			NetworkConnection *conn =
				container_of(info, NetworkConnection, ipv4.conn_info);

			if (conn->udp.callback) {
				conn->udp.callback(conn, net_buffer);
			} else {
				goto drop;
			}

			return PROTO_OK;
		}
	}
	if (info->list.next == &udp_lh) {
		// 没有找到匹配的连接，丢弃数据包
	drop:
		kfree(net_buffer->ptr);
		kfree(net_buffer);
		return PROTO_DROP;
	}

	return PROTO_OK;
}
