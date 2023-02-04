#include <kernel/driver.h>
#include <kernel/memory.h>
#include <network/ipv4.h>
#include <network/network.h>
#include <network/udp.h>
#include <stdint.h>

void udp_send(device_t *device, uint8_t *src_mac, uint8_t *src_ip, uint8_t *dst_mac, uint8_t *dst_ip,
			  uint16_t src_port, uint16_t dst_port, uint16_t *data, uint16_t datalen) {
	uint32_t	chksum = 0, i;
	udp_head_t *udp_head;
	uint16_t   *buf = kmalloc(sizeof(udp_head_t) + datalen);
	udp_head		= (udp_head_t *)buf;

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

	ipv4_send(device, src_mac, src_ip, dst_mac, dst_ip, 0, 64, PROTOCOL_UDP, (uint8_t *)buf,
			  sizeof(udp_head_t) + datalen);
	kfree(buf);
}