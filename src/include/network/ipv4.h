#ifndef IPV4_H
#define IPV4_H

#include "../stdint.h"

#define IPV4_FLAG_DF 0x02
#define IPV4_FLAG_MF 0x04

typedef struct {
	uint8_t	 IHL	 : 4;
	uint8_t	 Version : 4;
	uint8_t	 TypeOfService;
	uint16_t TotalLength;
	uint16_t Identification;
	uint16_t Offset;
	uint8_t	 TimetoLive;
	uint8_t	 Protocol;
	uint16_t HeaderChecksum;
	uint8_t	 SourceAddress[4];
	uint8_t	 DestinationAddress[4];
	uint8_t *Options;
} __attribute__((packed)) ipv4_header_t;

void ipv4_send(device_t *device, uint8_t *src_mac, uint8_t *src_ip, uint8_t *dst_mac, uint8_t *dst_ip,
			   uint8_t DF, uint8_t ttl, uint8_t protocol, uint8_t *data, uint8_t datalen);

#endif