#ifndef _ICMP_H
#define _ICMP_H

#include <driver/network/buffer.h>
#include <driver/network/conn.h>
#include <driver/network/protocols/ipv4/ipv4.h>
#include <driver/network/protocols/protocols.h>
#include <stdint.h>

#define ICMP_TYPE_ECHO_REPLY	   0
#define ICMP_TYPE_DEST_UNREACHABLE 3
#define ICMP_TYPE_SOURCE_QUENCH	   4
#define ICMP_TYPE_REDIRECT		   5
#define ICMP_TYPE_ECHO			   8
#define ICMP_TYPE_TIME_EXCEEDED	   11
#define ICMP_TYPE_PARAM_PROBLEM	   12
#define ICMP_TYPE_TIMESTAMP		   13
#define ICMP_TYPE_TIMESTAMP_REPLY  14
#define ICMP_TYPE_INFO_REQUEST	   15
#define ICMP_TYPE_INFO_REPLY	   16

typedef struct {
	uint8_t	 type;
	uint8_t	 code;
	uint16_t checksum;
	union {
		uint32_t unused;
		struct {
			uint8_t pointer;
			uint8_t unused[3];
		} param;
		uint8_t gateway_ip[4];
		struct {
			uint16_t id;
			uint16_t seq;
		} info;
	};
} __attribute__((packed)) IcmpHeader;

ProtocolResult icmp_recv(
	NetworkDevice *device, NetBuffer *net_buffer, Ipv4Header *header,
	ProtocolReplyCallback *stack, int stack_size);

#endif