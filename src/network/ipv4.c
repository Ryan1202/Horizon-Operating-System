#include <kernel/driver.h>
#include <kernel/memory.h>
#include <network/ipv4.h>
#include <network/network.h>

void ipv4_send(device_t *device, uint8_t *src_mac, uint8_t *src_ip, uint8_t *dst_mac, uint8_t *dst_ip,
			   uint8_t DF, uint8_t ttl, uint8_t protocol, uint8_t *data, uint8_t datalen) {
	int			   i;
	uint32_t	   chksum = 0;
	ipv4_header_t *header;

	uint16_t *buf = kmalloc(20 + datalen);
	header		  = (ipv4_header_t *)buf;

	header->Version		   = 4;
	header->IHL			   = 20 / 4;
	header->TypeOfService  = 0;
	header->TotalLength	   = HOST2BE_WORD(20 + datalen);
	header->Identification = HOST2BE_WORD(0);
	header->Offset		   = HOST2BE_WORD(0 | (DF << 14));
	header->TimetoLive	   = ttl;
	header->Protocol	   = protocol;
	header->HeaderChecksum = 0; // 先置0防止影响checksum计算
	memcpy(header->SourceAddress, src_ip, 4);
	memcpy(header->DestinationAddress, dst_ip, 4);
	memcpy((uint8_t *)buf + 20, data, datalen);

	for (i = 0; i < 20 / sizeof(uint16_t); i++) {
		chksum += BE2HOST_WORD(buf[i]);
	}
	header->HeaderChecksum = HOST2BE_WORD(~(uint16_t)((chksum & 0xffff) + (chksum >> 16)));

	if (device->type == DEV_ETH_NET) {
		eth_write(device, src_mac, dst_mac, ETH_TYPE_IPV4, (uint8_t *)buf, 20 + datalen);
	}
	kfree(buf);
}