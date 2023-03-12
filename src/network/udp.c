#include <kernel/driver.h>
#include <kernel/memory.h>
#include <network/ipv4.h>
#include <network/network.h>
#include <network/udp.h>
#include <stdint.h>

void udp_send(netc_t *netc, uint8_t *dst_ip, uint16_t src_port, uint16_t dst_port, uint16_t *data,
			  uint16_t datalen) {
	uint32_t	  chksum = 0, i;
	udp_head_t	 *udp_head;
	net_device_t *net_dev = netc->net_dev;
	device_t	 *device  = net_dev->device;
	uint16_t	 *buf	  = kmalloc(sizeof(udp_head_t) + datalen);
	uint8_t		  src_ip[4];
	udp_head = (udp_head_t *)buf;
	ipv4_get_ip(netc, src_ip);

	udp_head->SourcePort	  = HOST2BE_WORD(src_port);
	udp_head->DestinationPort = HOST2BE_WORD(dst_port);
	udp_head->Length		  = HOST2BE_WORD(sizeof(udp_head_t) + datalen);
	udp_head->Checksum		  = 0; // 为了方便计算，先置0
	memcpy((uint8_t *)buf + sizeof(udp_head_t), data, datalen);

	// 计算校验和
	// UDP伪首部
	for (i = 0; i < 4 / sizeof(uint16_t); i++) {
		chksum += (src_ip[i * 2] << 8) + src_ip[i * 2 + 1];
		chksum += (dst_ip[i * 2] << 8) + dst_ip[i * 2 + 1];
	}
	chksum += PROTOCOL_UDP;
	chksum += HOST2BE_WORD(udp_head->Length);
	// UDP首部
	for (i = 0; i < (sizeof(udp_head_t) + datalen) / sizeof(uint16_t); i++) {
		chksum += BE2HOST_WORD(buf[i]);
	}
	udp_head->Checksum = HOST2BE_WORD(~(uint16_t)((chksum & 0xffff) + (chksum >> 16)));

	ipv4_send(netc, dst_ip, 0, 64, PROTOCOL_UDP, (uint8_t *)buf, sizeof(udp_head_t) + datalen);
	kfree(buf);
}

void udp_read(netc_t *netc, uint8_t *buf, uint16_t offset, uint16_t length) {
	udp_head_t *udp_head = (udp_head_t *)(buf + offset);

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