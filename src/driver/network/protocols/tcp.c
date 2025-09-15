/**
 * @file tcp.c
 * @author Jiajun Wang (ryan1202@foxmail.com)
 * @brief TCP
 * References:
 * RFC793: Transmission Control Protocol
 * RFC6528: Defending against Sequence Number Attacks
 * RFC6298: Computing TCP's Retransmission Timer
 *
 */
#include "bits.h"
#include "driver/network/buffer.h"
#include "driver/network/network_dm.h"
#include "driver/network/protocols/protocols.h"
#include "driver/timer_dm.h"
#include "kernel/memory.h"
#include "kernel/spinlock.h"
#include "kernel/thread.h"
#include "math.h"
#include <driver/network/conn.h>
#include <driver/network/protocols/ipv4/icmp.h>
#include <driver/network/protocols/ipv4/ipv4.h>
#include <driver/network/protocols/tcp.h>
#include <hash.h>
#include <kernel/list.h>
#include <random.h>
#include <stdint.h>
#include <string.h>

#define TCP_CHECKSUM_ADD(sum, data) \
	sum += (data);                  \
	if (sum > 0xFFFF) sum = (sum & 0xFFFF) + (sum >> 16);

SPINLOCK(tcp_lock);
LIST_HEAD(tcp_lh);

const int send_win_default_size = 32768;
const int recv_win_default_size = 32768;

const int min_ephemeral_port = 32768;
const int max_ephemeral_port = 65535;

const int tcp_max_retries = 15;

const int tcp_fin_timeout = 60 * 1000;

void tcp_timeout_handler(void *arg);
void tcp_reset_conn(NetworkConnection *conn, Tcp *tcp);
void tcp_send(Tcp *tcp, NetworkConnection *conn, bool set_timer);

uint16_t tcp_checksum(
	TcpHeader *header, int length, const uint8_t *src_ip, const uint8_t *dst_ip,
	int ip_len) {
	uint8_t *data = (uint8_t *)header;
	uint32_t sum  = 0;

	// 清除校验和
	header->checksum = 0;

	// 计算伪头部校验和
	for (int j = 0; j < ip_len; j += 2) {
		sum += (src_ip[j] << 8 | src_ip[j + 1]);
		sum += (dst_ip[j] << 8 | dst_ip[j + 1]);
	}
	sum += IP_PROTO_TCP;
	sum += length;

	// 计算TCP头部和数据的校验和
	int i;
	for (i = 0; i + 2 <= length; i += 2) {
		sum += data[i] << 8 | data[i + 1];
		sum = (sum & 0xffff0000) ? ((sum >> 16) + (sum & 0xffff)) : sum;
	}

	if (length % 2) { // 如果长度是奇数，补齐一个字节
		sum += data[length - 1] << 8;
	}

	while (sum & 0xffff0000) {
		sum = (sum >> 16) + (sum & 0xffff);
	}

	sum = ~((uint16_t)(sum)) & 0xffff;
	return sum;
}

void tcp_register(NetworkConnection *conn) {
	conn->trans_protocol = TRANS_PROTO_TCP;
	if (conn->tcp.info == NULL) { conn->tcp.info = kmalloc(sizeof(Tcp)); }
	NET_BUF_RESV_HEAD(conn, sizeof(TcpHeader));
}

uint32_t tcp_generate_isn(void *ip_port_pair, int len) {
	static uint32_t counter = 0;
	uint32_t		isn		= timer_get_counter();
	counter++;

	isn += fnv1_hash_32(ip_port_pair, len);
	isn += counter;

	return isn;
}

void tcp_compute_retransmission_timer(Tcp *tcp, int r) {
	if (tcp == NULL) return;

	if (r == 0) {
		if (tcp->rttvar == 0) {
			// 还没完成RTT测量
			tcp->rto = 1 * 1000;
		}
	} else {
		if (tcp->rttvar == 0) {
			// 完成初次RTT测量
			tcp->srtt	= r;
			tcp->rttvar = r / 2;
			tcp->rto	= tcp->srtt + MAX(1 * 1000, 4 * tcp->rttvar);
		} else {
			// 完成后续RTT测量
			tcp->rttvar = (3 * tcp->rttvar + 1 * abs(tcp->srtt - r)) / 4;
			tcp->srtt	= (7 * tcp->srtt + 1 * r) / 8;
			tcp->rto	= tcp->srtt + MAX(1 * 1000, 4 * tcp->rttvar);
		}
		tcp->rto = MIN(tcp->rto, 60 * 1000); // 最长1分钟
	}
}

ProtocolResult tcp_bind(NetworkConnection *conn, uint16_t port) {
	if (conn->tcp.info == NULL) { return PROTO_ERROR_NULL_PTR; }
	Ipv4ConnInfo *conn_info = &conn->ipv4.conn_info;
	if (list_in_list(&conn_info->list))
		return PROTO_ERROR_REBIND; // 已经绑定了端口

	Ipv4ConnInfo *info;
	spin_lock(&tcp_lock);
	list_for_each_owner (info, &tcp_lh, list) {
		if (info->local.port == port ||
			memcmp(info->local.ip, conn_info->local.ip, 4)) {
			spin_unlock(&tcp_lock);
			return PROTO_ERROR_PORT_CONFLICT; // 端口冲突
		}
	}
	conn_info->local.port = port;

	Tcp *tcp			 = conn->tcp.info;
	tcp->mss			 = conn->pmtu - sizeof(Ipv4Header) - sizeof(TcpHeader);
	tcp->cur.seq		 = 0;
	tcp->cur.ack		 = 0;
	tcp->state			 = TCP_STATE_CLOSED;
	tcp->recv_window	 = kmalloc(recv_win_default_size);
	tcp->recv.next		 = 0;
	tcp->recv.window	 = recv_win_default_size;
	tcp->recv.total_size = recv_win_default_size;
	tcp->recv.read		 = 0;
	tcp->rttvar = tcp->rto = tcp->srtt = 0;
	tcp_compute_retransmission_timer(tcp, 0);
	timer_init(&tcp->timeout_timer);
	tcp->timeout_timer.callback = tcp_timeout_handler;
	tcp->timeout_timer.arg		= tcp;

	conn->tcp.info->conn   = conn;
	conn->tcp.info->thread = get_current_thread();

	list_add_tail(&conn->ipv4.conn_info.list, &tcp_lh);
	spin_unlock(&tcp_lock);
	return PROTO_OK;
}

ProtocolResult tcp_connect(
	NetworkConnection *conn, uint8_t *dst_ip, uint16_t dst_port) {
	Tcp *tcp = conn->tcp.info;
	if (conn->tcp.info == NULL) { return PROTO_ERROR_NULL_PTR; }
	if (tcp->state != TCP_STATE_CLOSED) {
		// 如果没有绑定端口，随机绑定一个大于32768的端口
		uint16_t port = min_ephemeral_port +
						(rand() % (max_ephemeral_port - min_ephemeral_port));
		int count = max_ephemeral_port - min_ephemeral_port + 1;
		while (count-- > 0) {
			ProtocolResult result = tcp_bind(conn, port);
			if (result == PROTO_OK) break;
			else if (result != PROTO_ERROR_PORT_CONFLICT) { return result; }
			port += 1;
		}
	}

	Ipv4ConnInfo *conn_info = &conn->ipv4.conn_info;
	memcpy(conn_info->remote.ip, dst_ip, 4);
	conn_info->remote.port = dst_port;

	tcp->send.iss =
		tcp_generate_isn(&conn_info, sizeof(Ipv4ConnInfo) - sizeof(list_t));
	tcp->cur.seq = tcp->send.iss;
	// 构造SYN报文
	net_buffer_header_alloc(conn->buffer, sizeof(TcpHeader));
	tcp->header = (TcpHeader *)conn->buffer->head;

	int		   option_len  = 4 /* Max Segment Size */;
	TcpHeader *header	   = tcp->header;
	header->src_port	   = HOST2BE_WORD(conn_info->local.port);
	header->dest_port	   = HOST2BE_WORD(conn_info->remote.port);
	header->seq			   = HOST2BE_DWORD(tcp->cur.seq);
	header->ack			   = 0;
	header->data_offset	   = ((sizeof(TcpHeader) + option_len) / 4) << 4;
	header->flags		   = TCP_FLAG_SYN;
	header->window_size	   = HOST2BE_WORD(tcp->recv.window);
	header->urgent_pointer = 0;

	// TCP Options算在NetBuffer的data区域内
	header->options[0]				   = TCP_OPTION_MSS;
	header->options[1]				   = 4;
	*(uint16_t *)(header->options + 2) = HOST2BE_WORD(tcp->mss);
	net_buffer_put(conn->buffer, option_len);

	header->checksum = 0;
	int length		 = sizeof(TcpHeader) + CONN_CONTENT_SIZE(conn);
	header->checksum = HOST2BE_WORD(tcp_checksum(
		header, length, conn_info->local.ip, conn_info->remote.ip, 4));

	conn_wrap(conn, PROTO_LEVEL_NETWORK);

	tcp->state	= TCP_STATE_SYN_SENT;
	conn->state = CONN_STATE_OPENING;

	timer_set_timeout(
		&tcp->timeout_timer, timer_count_ms(&tcp->timeout_timer, tcp->rto));
	timer_callback_enable(&tcp->timeout_timer);
	NETWORK_SEND(conn->net_device, conn);

	thread_set_status(TASK_INTERRUPTIBLE);
	thread_wait();

	if (tcp->state != TCP_STATE_ESTABLISHED) return PROTO_ERROR_CONNECT_FAILED;
	// 建立连接后再真正分配发送窗口
	tcp->send.unack		 = 0;
	tcp->send.next		 = 0;
	tcp->send.wl1		 = tcp->cur.ack;
	tcp->send.wl2		 = tcp->cur.seq;
	tcp->send.total_size = send_win_default_size;
	tcp->send.window = MIN(tcp->send.total_size, tcp->send.remote_window_size);
	tcp->send_window = kmalloc(tcp->send.total_size);
	tcp->send_time	 = timer_get_counter();

	return PROTO_OK;
}

ProtocolResult tcp_send_data(
	NetworkConnection *conn, uint8_t *buf, int length) {
	if (conn == NULL || buf == NULL || length <= 0) {
		return PROTO_ERROR_NULL_PTR;
	}

	Tcp *tcp = conn->tcp.info;
	if (tcp == NULL) { return PROTO_ERROR_NULL_PTR; }

	int used_space = tcp->send.next - tcp->send.unack;
	used_space =
		(used_space >= 0) ? used_space : used_space + tcp->send.total_size;
	int space = tcp->send.total_size - used_space;
	if (space < length) { return PROTO_ERROR_NO_SPACE_LEFT; }

	// 将数据放入发送缓冲区
	if (tcp->send.unack < tcp->send.next) {
		int size1 = MIN(length, tcp->send.total_size - tcp->send.next);
		int size2 = MAX(length - size1, 0);
		memcpy(tcp->send_window + tcp->send.next, buf, size1);
		memcpy(tcp->send_window, buf + size1, size2);
	} else {
		int size = MIN(length, space);
		memcpy(tcp->send_window + tcp->send.next, buf, size);
	}
	tcp->send.next = (tcp->send.next + length) % tcp->send.total_size;
	used_space += length;

	// 发送数据
	int total_size = 0;
	int max		   = MIN(tcp->mss, tcp->send.window);
	if (tcp->send.unack < tcp->send.next) {
		int size = MIN(max, used_space);
		memcpy(conn->buffer->data, tcp->send_window + tcp->send.unack, size);
		tcp->send.unack += size;
		tcp->send.unack %= tcp->send.total_size;
		total_size = size;
	} else {
		int size1 = MIN(max, tcp->send.total_size - tcp->send.unack);
		int size2 = MIN(MIN(max - size1, tcp->send.next), used_space - size1);
		memcpy(conn->buffer->data, tcp->send_window + tcp->send.unack, size1);
		memcpy(conn->buffer->data + size1, tcp->send_window, size2);
		tcp->send.next += size1 + size2;
		tcp->send.next %= tcp->send.total_size;
		total_size = size1 + size2;
	}
	net_buffer_put(conn_buffer(conn), total_size);
	tcp->header->flags = TCP_FLAG_PSH;
	tcp->header->flags |= TCP_FLAG_ACK;
	tcp_send(tcp, conn, true);
	return PROTO_OK;
}

ProtocolResult tcp_half_close(NetworkConnection *conn, Tcp *tcp) {
	while (tcp->send.unack < tcp->send.next) {}
	net_buffer_clean_data(conn->buffer);
	tcp->header->flags = TCP_FLAG_FIN | TCP_FLAG_ACK;
	tcp->state		   = TCP_STATE_FIN_WAIT1;
	conn->state		   = CONN_STATE_CLOSING;

	kfree(tcp->send_window);
	tcp->send.window = 0;
	tcp->send_window = NULL;
	tcp_send(tcp, conn, true);
	return PROTO_OK;
}

ProtocolResult tcp_shutdown(NetworkConnection *conn, uint8_t how) {
	if (conn == NULL) return PROTO_ERROR_NULL_PTR;
	if (conn->trans_protocol != TRANS_PROTO_TCP) return PROTO_ERROR_UNSUPPORT;

	Tcp *tcp = conn->tcp.info;
	if (tcp == NULL) return PROTO_ERROR_NULL_PTR;

	switch (how) {
	case TCP_SHUT_RD:
		kfree(tcp->recv_window);
		tcp->recv_window = NULL;
		break;
	case TCP_SHUT_WR:
		tcp_half_close(conn, tcp);
		break;
	case TCP_SHUT_RDWR:
		kfree(tcp->recv_window);
		tcp->recv_window = NULL;
		tcp_half_close(conn, tcp);
		break;
	default:
		return PROTO_ERROR_UNSUPPORT;
	}

	return PROTO_OK;
}

void tcp_reset(NetworkConnection *conn) {
	if (conn == NULL) return;
	if (conn->trans_protocol != TRANS_PROTO_TCP) return;
	Tcp *tcp = conn->tcp.info;
	tcp_reset_conn(conn, tcp);
}

void tcp_reset_conn(NetworkConnection *conn, Tcp *tcp) {
	if (conn == NULL || tcp == NULL) return;

	net_buffer_clean_data(conn->buffer);
	tcp->state			  = TCP_STATE_CLOSED;
	TcpHeader *tcp_header = tcp->header;
	tcp_header->flags	  = TCP_FLAG_RST;
	tcp_header->ack		  = 0;
	tcp_header->checksum  = HOST2BE_WORD(tcp_checksum(
		 tcp_header, sizeof(TcpHeader), conn->ipv4.conn_info.local.ip,
		 conn->ipv4.conn_info.remote.ip, 4));
	kfree(tcp->send_window);
	kfree(tcp->recv_window);

	NETWORK_SEND(conn->net_device, conn);
}

void tcp_send(Tcp *tcp, NetworkConnection *conn, bool set_timer) {
	TcpHeader *tcp_header	= tcp->header;
	tcp_header->seq			= HOST2BE_DWORD(tcp->cur.seq);
	tcp_header->ack			= HOST2BE_DWORD(tcp->cur.ack);
	tcp_header->window_size = HOST2BE_WORD(tcp->recv.window);
	tcp_header->data_offset = ((sizeof(TcpHeader)) >> 2) << 4;

	tcp_header->checksum = tcp_checksum(
		tcp_header, CONN_CONTENT_SIZE(conn) + sizeof(TcpHeader),
		conn->ipv4.conn_info.local.ip, conn->ipv4.conn_info.remote.ip, 4);
	tcp_header->checksum = HOST2BE_WORD(tcp_header->checksum);

	if (tcp->send_time == 0) {
		tcp->send_time = timer_get_counter();
		tcp->send_seq  = tcp->cur.seq;
	}
	// 如果没有设置重传计时则设置一个
	if (set_timer && timer_is_timeout(&tcp->timeout_timer)) {
		timer_set_timeout(
			&tcp->timeout_timer, timer_count_ms(&tcp->timeout_timer, tcp->rto));
		timer_callback_enable(&tcp->timeout_timer);
	}
	ipv4_rewrap(conn);
	NETWORK_SEND(conn->net_device, conn);
}

void tcp_ack(NetworkConnection *conn, Tcp *tcp, uint8_t extra_flag) {
	if (conn == NULL || tcp == NULL) return;

	TcpHeader *tcp_header = tcp->header;

	tcp_header->flags = extra_flag;

	tcp_header->flags |= TCP_FLAG_ACK;
	tcp_send(tcp, conn, true);
}

void tcp_ack_handler(
	Tcp *tcp, TcpHeader *header, NetBuffer *net_buffer, int length) {
	uint32_t seq = BE2HOST_DWORD(header->seq);
	uint32_t ack = BE2HOST_DWORD(header->ack);

	if (length > 0) {
		if (ack >= tcp->send_seq && tcp->send_seq != 0) {
			int rtt = timer_get_counter() - tcp->send_time;
			tcp_compute_retransmission_timer(tcp, rtt);
			tcp->send_seq = 0;
		}
		if (tcp->recv_window != NULL) {
			int size1 = MIN(tcp->recv.total_size - tcp->recv.next, length);
			// size2 = min(length - size1, tcp->recv.window - size1)
			// 由于由发送方保证了tcp->recv.window ≥ length，所以直接使用length -
			// size1
			int size2 = length - size1;
			memcpy(tcp->recv_window + tcp->recv.next, net_buffer->data, size1);
			memcpy(tcp->recv_window, net_buffer->data + size1, size2);
			tcp->recv.next += length;
			tcp->recv.next %= tcp->recv.total_size;
			tcp->recv.window -= length;
		}

		tcp->send.wl1 = seq;
		tcp->cur.ack  = seq + length;
		tcp_ack(tcp->conn, tcp, 0);
	}

	uint32_t acked = ack - tcp->send.wl2;
	if (acked > 0) {
		tcp->send.unack += acked;
		tcp->send.unack %= tcp->send.total_size;
		tcp->send.wl2 = ack;

		tcp->cur.seq += acked;
		net_buffer_clean_data(tcp->conn->buffer);
	}
}

void tcp_timeout_handler(void *arg) {
	if (arg == NULL) return;
	Tcp *tcp = arg;

	switch (tcp->state) { // 超时重传
	case TCP_STATE_SYN_SENT:
	case TCP_STATE_SYN_RECEIVED:
	case TCP_STATE_ESTABLISHED:
	case TCP_STATE_FIN_WAIT1:
		tcp->retry_times++;
		if (tcp->retry_times < tcp_max_retries) {
			tcp->rto <<= 1; // 指数退避
			tcp->rto = MIN(tcp->rto, 60 * 1000);
			if (tcp->state == TCP_STATE_SYN_SENT ||
				tcp->state == TCP_STATE_SYN_RECEIVED)
				tcp->rto = MIN(tcp->rto, 3 * 1000);
			timer_set_timeout(
				&tcp->timeout_timer,
				timer_count_ms(&tcp->timeout_timer, tcp->rto));
			timer_callback_enable(&tcp->timeout_timer);
			tcp_send(tcp, tcp->conn, true);
		} else {
			tcp_reset_conn(tcp->conn, tcp);
		}
		break;
	case TCP_STATE_FIN_WAIT2:
		// 进入FIN_WAIT_2后对方一直不发送FIN，强制关闭
		tcp_reset_conn(tcp->conn, tcp);
		break;
	case TCP_STATE_TIME_WAIT:
		// 2MSL时间到，关闭连接
		tcp->state		 = TCP_STATE_CLOSED;
		tcp->conn->state = CONN_STATE_CLOSED;
		kfree(tcp->recv_window);
		tcp->recv_window = NULL;
		break;
	default:
		break;
	}
}

void tcp_options_handler(
	TcpHeader *tcp_header, TcpOptionIndex indexes[TOI_MAX]) {
	int options_size =
		((tcp_header->data_offset & 0xf0) >> 2) - sizeof(TcpHeader);
	if (options_size <= 0) return;

	uint8_t *options = (uint8_t *)(tcp_header + 1);
	int		 i		 = 0;
	while (i < options_size) {
		uint8_t kind = options[i];
		i++;
		if (kind == TCP_OPTION_END) break;
		else if (kind == TCP_OPTION_NOP) continue;
		else {
			if (i >= options_size) break;
			uint8_t length = options[i];
			if (i + length - 1 > options_size) break;
			switch (kind) {
			case TCP_OPTION_MSS:
				indexes[TOI_MSS] = i + 1;
				break;
			default:
				break;
			}
			i += length - 1;
		}
	}
}

void tcp_rx_handler(
	NetworkConnection *conn, TcpHeader *tcp_header, NetBuffer *net_buffer,
	int length) {
	Tcp		*tcp	  = conn->tcp.info;
	bool	 syn_flag = tcp_header->flags & TCP_FLAG_SYN;
	bool	 ack_flag = tcp_header->flags & TCP_FLAG_ACK;
	uint32_t seq	  = BE2HOST_DWORD(tcp_header->seq);
	uint32_t ack	  = BE2HOST_DWORD(tcp_header->ack);

	uint8_t extra_flags = 0;
	if (tcp_header->flags & TCP_FLAG_RST) {
		tcp->state	= TCP_STATE_CLOSED;
		conn->state = CONN_STATE_CLOSED;
	}

	TcpOptionIndex indexes[TOI_MAX] = {TOI_MAX};
	tcp_options_handler(tcp_header, indexes);
	switch (tcp->state) {
	case TCP_STATE_CLOSED:
		tcp_reset_conn(conn, tcp);
		break;
	case TCP_STATE_LISTEN:
		if (syn_flag) {
			if (indexes[TOI_MSS] < TOI_MAX) {
				uint16_t mss = BE2HOST_WORD(
					*(uint16_t *)(tcp_header->options + indexes[TOI_MSS]));
				tcp->mss = MIN(mss, tcp->mss);
			}

			tcp->recv.irs = seq;
			tcp->cur.ack  = seq + 1;
			tcp->send.remote_window_size =
				BE2HOST_WORD(tcp_header->window_size);
			tcp->state = TCP_STATE_SYN_RECEIVED;
			extra_flags |= TCP_FLAG_SYN;
			net_buffer_clean_data(tcp->conn->buffer);
			tcp_ack(conn, tcp, extra_flags);
		}
		break;
	case TCP_STATE_SYN_SENT:
		// 处理SYN
		if (!syn_flag) {
			// 异常，重置连接
			tcp_reset_conn(conn, tcp);
			break;
		} else {
			tcp->send.remote_window_size =
				BE2HOST_WORD(tcp_header->window_size);
			tcp->recv.irs = seq;
			tcp->cur.ack  = seq + 1;
			tcp->state	  = TCP_STATE_SYN_RECEIVED;
		}
	case TCP_STATE_SYN_RECEIVED:
		if (indexes[TOI_MSS] < TOI_MAX) {
			uint16_t mss = BE2HOST_WORD(
				*(uint16_t *)(tcp_header->options + indexes[TOI_MSS]));
			tcp->mss = MIN(mss, tcp->mss);
		}
		timer_callback_cancel(&tcp->timeout_timer);
		// 处理ACK
		if (ack != tcp->cur.seq + 1) {
			tcp_reset_conn(conn, tcp);
			break;
		}
		if (ack_flag) {
			tcp->cur.seq = ack;
			tcp->state	 = TCP_STATE_ESTABLISHED;
			conn->state	 = CONN_STATE_OPENED;
		}
		net_buffer_clean_data(tcp->conn->buffer);
		tcp_ack(conn, tcp, extra_flags);
		thread_unblock(tcp->thread);
		break;
	case TCP_STATE_ESTABLISHED:
		tcp_ack_handler(tcp, tcp_header, net_buffer, length);
		if (tcp_header->flags & TCP_FLAG_FIN) {
			tcp->cur.seq = ack;
			tcp->cur.ack = seq + 1;
			tcp->state	 = TCP_STATE_CLOSE_WAIT;
			conn->state	 = CONN_STATE_CLOSING;
			net_buffer_clean_data(tcp->conn->buffer);
			tcp_ack(conn, tcp, extra_flags);
			break;
		}
		break;
	case TCP_STATE_CLOSING:
		if (ack_flag && ack == tcp->cur.seq + 1) {
			tcp->cur.seq = ack;
			tcp->state	 = TCP_STATE_CLOSE_WAIT;
			extra_flags |= TCP_FLAG_FIN;
		}
		net_buffer_clean_data(tcp->conn->buffer);
		tcp_ack(conn, tcp, extra_flags);
		break;
	case TCP_STATE_CLOSE_WAIT:
		tcp_reset_conn(conn, tcp);
		break;
	case TCP_STATE_FIN_WAIT1:
		if (ack_flag) tcp_ack_handler(tcp, tcp_header, net_buffer, length);
		if (ack_flag && ack == tcp->cur.seq + 1) {
			tcp->cur.seq = ack;
			tcp->state	 = TCP_STATE_FIN_WAIT2;
			extra_flags |= TCP_FLAG_FIN;

			timer_callback_cancel(&tcp->timeout_timer);
			timer_set_timeout(
				&tcp->timeout_timer,
				timer_count_ms(&tcp->timeout_timer, tcp_fin_timeout));
			timer_callback_enable(&tcp->timeout_timer);
		}
		if (tcp_header->flags & TCP_FLAG_FIN) {
			tcp->cur.ack = seq + 1;
			tcp->state	 = TCP_STATE_CLOSING;
		}
		break;
	case TCP_STATE_FIN_WAIT2:
		if (ack_flag) tcp_ack_handler(tcp, tcp_header, net_buffer, length);
		if (tcp_header->flags & TCP_FLAG_FIN) {
			tcp->cur.ack = seq + 1;
			tcp->state	 = TCP_STATE_TIME_WAIT;
			extra_flags |= TCP_FLAG_ACK;

			timer_callback_cancel(&tcp->timeout_timer);
			uint32_t count = timer_count_ms(&tcp->timeout_timer, 2 * TCP_MSL);
			timer_set_timeout(&tcp->timeout_timer, count);
			timer_callback_enable(&tcp->timeout_timer);
			net_buffer_clean_data(tcp->conn->buffer);
			tcp_ack(conn, tcp, extra_flags);
		}
		break;
	case TCP_STATE_LAST_ACK:
		if (ack_flag && ack == tcp->cur.seq + 1) {
			tcp->cur.seq = ack;
			tcp->state	 = TCP_STATE_CLOSED;
			conn->state	 = CONN_STATE_CLOSED;
			thread_unblock(tcp->thread);
		}
		break;
	case TCP_STATE_TIME_WAIT:
		tcp_reset_conn(conn, tcp);
		break;
	}
}

ProtocolResult tcp_recv(
	NetBuffer *net_buffer, int length, uint8_t *src_ip, uint8_t *dst_ip,
	int ip_len) {
	TcpHeader *tcp_header = (TcpHeader *)net_buffer->data;

	int header_length = (tcp_header->data_offset >> 4) * 4;
	if (header_length < sizeof(TcpHeader))
		return PROTO_ERROR_UNSUPPORT; // 数据包太小
	int data_length = length - header_length;

	uint16_t checksum = tcp_header->checksum;
	uint16_t calc_checksum =
		tcp_checksum(tcp_header, length, src_ip, dst_ip, ip_len);
	if (HOST2BE_WORD(calc_checksum) != checksum) return PROTO_ERROR_CHECKSUM;

	net_buffer->data += header_length;

	if (list_empty(&tcp_lh)) {
		// 没有绑定的连接
		goto drop;
	}
	Ipv4ConnInfo *info, *next;
	spin_lock(&tcp_lock);
	list_for_each_owner_safe (info, next, &tcp_lh, list) {
		if (info->local.port == BE2HOST_WORD(tcp_header->dest_port) &&
			(info->remote.port == BE2HOST_WORD(tcp_header->src_port))) {
			if (memcmp(info->local.ip, dst_ip, ip_len) == 0 &&
				memcmp(info->remote.ip, src_ip, ip_len) == 0) {
				NetworkConnection *conn =
					container_of(info, NetworkConnection, ipv4.conn_info);
				tcp_rx_handler(conn, tcp_header, net_buffer, data_length);

				spin_unlock(&tcp_lock);
				return PROTO_OK;
			}
		}
	}
	// 没有找到匹配的连接，丢弃数据包
drop:
	// TODO: 改成发送RST
	kfree(net_buffer->ptr);
	kfree(net_buffer);
	return PROTO_DROP;
}

void tcp_notify_unreachable(
	void *data, uint8_t *src_ip, uint8_t *dst_ip, int ip_len, int code) {
	Ipv4ConnInfo	  *info, *next;
	NetworkConnection *conn;
	TcpHeader		  *header = (TcpHeader *)data;
	bool			   flag	  = false;

	spin_lock(&tcp_lock);
	list_for_each_owner_safe (info, next, &tcp_lh, list) {
		if (info->local.port == BE2HOST_WORD(header->dest_port) &&
			(info->remote.port == BE2HOST_WORD(header->src_port))) {
			if (memcmp(info->local.ip, dst_ip, ip_len) == 0 &&
				memcmp(info->remote.ip, src_ip, ip_len) == 0) {
				flag = true;
				break;
			}
		}
	}
	spin_unlock(&tcp_lock);
	if (!flag) return;

	conn = container_of(info, NetworkConnection, ipv4.conn_info);
	switch (code) {
	case ICMP_UNREACHABLE_NET:
		tcp_reset_conn(conn, conn->tcp.info);
		conn->state = CONN_STATE_NET_UNREACHABLE;
		break;
	case ICMP_UNREACHABLE_HOST:
		tcp_reset_conn(conn, conn->tcp.info);
		conn->state = CONN_STATE_HOST_UNREACHABLE;
		break;
	default:
		break;
	}
}

void tcp_update_mtu(uint8_t *src_ip, uint8_t *dst_ip, int ip_len, int mtu) {
	Ipv4ConnInfo	  *info, *next;
	NetworkConnection *conn;
	bool			   flag = false;

	spin_lock(&tcp_lock);
	list_for_each_owner_safe (info, next, &tcp_lh, list) {
		// if (memcmp(info->local.ip, dst_ip, ip_len) == 0 &&
		// 	memcmp(info->remote.ip, src_ip, ip_len) == 0) {
		flag = true;
		break;
		// }
	}
	spin_unlock(&tcp_lock);
	if (!flag) return;

	conn	 = container_of(info, NetworkConnection, ipv4.conn_info);
	Tcp *tcp = conn->tcp.info;
	if (mtu < conn->pmtu && mtu > 0) {
		mtu		   = MIN(MAX(IPv4_DEFAULT_MSS, mtu), conn->pmtu);
		conn->pmtu = mtu;
		tcp->mss   = mtu - sizeof(Ipv4Header) - sizeof(TcpHeader);
		if (conn->state == CONN_STATE_OPENED) {
			conn->buffer->tail =
				MIN(conn->buffer->tail, conn->buffer->data + tcp->mss);
			ipv4_rewrap(conn);
			NETWORK_SEND(conn->net_device, conn);
		}
	} else if (mtu > conn->pmtu) {
		conn->pmtu = mtu;
		tcp->mss   = mtu - sizeof(Ipv4Header) - sizeof(TcpHeader);
	}
}
