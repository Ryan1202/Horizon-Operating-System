#include "driver/network/network_dm.h"
#include <driver/network/conn.h>
#include <driver/network/protocols/ipv4/icmp.h>
#include <driver/network/protocols/ipv4/ipv4.h>
#include <driver/network/protocols/protocols.h>
#include <stdint.h>

ProtocolResult icmp_recv_unsupport(
	NetworkDevice *device, NetBuffer *net_buffer, Ipv4Header *header,
	IcmpHeader *icmp_header, ProtocolReplyCallback *stack, int stack_size);
ProtocolResult icmp_recv_echo_message(
	NetworkDevice *device, NetBuffer *net_buffer, Ipv4Header *header,
	IcmpHeader *icmp_header, ProtocolReplyCallback *stack, int stack_size);

ProtocolResult (*icmp_recv_handlers[17])(
	NetworkDevice *device, NetBuffer *net_buffer, Ipv4Header *header,
	IcmpHeader *icmp_header, ProtocolReplyCallback *stack, int stack_size) = {
	[ICMP_TYPE_ECHO]			 = icmp_recv_echo_message,
	[ICMP_TYPE_ECHO_REPLY]		 = icmp_recv_unsupport,
	[ICMP_TYPE_DEST_UNREACHABLE] = icmp_recv_unsupport,
	[ICMP_TYPE_SOURCE_QUENCH]	 = icmp_recv_unsupport,
	[ICMP_TYPE_REDIRECT]		 = icmp_recv_unsupport,
	[ICMP_TYPE_TIME_EXCEEDED]	 = icmp_recv_unsupport,
	[ICMP_TYPE_PARAM_PROBLEM]	 = icmp_recv_unsupport,
	[ICMP_TYPE_TIMESTAMP]		 = icmp_recv_unsupport,
	[ICMP_TYPE_TIMESTAMP_REPLY]	 = icmp_recv_unsupport,
	[ICMP_TYPE_INFO_REQUEST]	 = icmp_recv_unsupport,
	[ICMP_TYPE_INFO_REPLY]		 = icmp_recv_unsupport,
	[1 ... 2]					 = icmp_recv_unsupport,
	[6 ... 7]					 = icmp_recv_unsupport,
	[9 ... 10]					 = icmp_recv_unsupport,
};

uint16_t icmp_checksum(IcmpHeader *header, int length) {
	uint32_t  sum  = 0;
	uint16_t *data = (void *)header;

	header->checksum = 0;

	for (size_t i = 0; i < length / 2; i++) {
		sum += data[i];
		if (sum > 0xFFFF) sum = (sum & 0xFFFF) + (sum >> 16);
	}
	return ~((uint16_t)sum);
};

ProtocolResult icmp_recv(
	NetworkDevice *device, NetBuffer *net_buffer, Ipv4Header *header,
	ProtocolReplyCallback *stack, int stack_size) {
	IcmpHeader *icmp_header = (IcmpHeader *)net_buffer->data;
	if (net_buffer->tail - net_buffer->data < sizeof(IcmpHeader)) {
		return PROTO_ERROR_UNSUPPORT;
	}

	uint16_t checksum = icmp_header->checksum;
	if (icmp_checksum(icmp_header, net_buffer->tail - net_buffer->data) !=
		checksum) {
		return PROTO_ERROR_CHECKSUM;
	}

	if (icmp_header->type < 17) {
		return icmp_recv_handlers[icmp_header->type](
			device, net_buffer, header, icmp_header, stack, stack_size);
	}
	return PROTO_ERROR_UNSUPPORT;
}

ProtocolResult icmp_recv_unsupport(
	NetworkDevice *device, NetBuffer *net_buffer, Ipv4Header *header,
	IcmpHeader *icmp_header, ProtocolReplyCallback *stack, int stack_size) {
	return PROTO_ERROR_UNSUPPORT;
}

ProtocolResult icmp_recv_echo_message(
	NetworkDevice *device, NetBuffer *net_buffer, Ipv4Header *header,
	IcmpHeader *icmp_header, ProtocolReplyCallback *stack, int stack_size) {
	if (net_buffer->tail - net_buffer->data < 8) {
		return PROTO_ERROR_UNSUPPORT;
	}
	uint32_t ip					  = *((uint32_t *)header->dst_ip);
	*((uint32_t *)header->dst_ip) = *((uint32_t *)header->src_ip);
	*((uint32_t *)header->src_ip) = ip;
	ipv4_checksum(header);

	icmp_header->type	  = ICMP_TYPE_ECHO_REPLY;
	icmp_header->checksum = 0;
	icmp_header->checksum =
		icmp_checksum(icmp_header, net_buffer->tail - net_buffer->data);

	protocol_reply(device, net_buffer, stack, stack_size);

	return PROTO_OK;
}