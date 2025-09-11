#ifndef _DHCP_H
#define _DHCP_H

#include "driver/network/conn.h"
#include "driver/network/network_dm.h"
#include "driver/timer_dm.h"
#include "math.h"
#include "stdint.h"

#define DHCP_OP_BOOTREQUEST 1
#define DHCP_OP_BOOTREPLY	2
#define DHCP_HTYPE_ETHERNET 1

/**
 * DHCP Options
 * RFC 1533
 */
#define DHCP_OPTION_PAD							0
#define DHCP_OPTION_SUBNET_MASK					1
#define DHCP_OPTION_TIME_OFFSET					2
#define DHCP_OPTION_ROUTER						3
#define DHCP_OPTION_TIMER_SERVER				4
#define DHCP_OPTION_NAME_SERVER					5
#define DHCP_OPTION_DOMAIN_NAME_SERVER			6
#define DHCP_OPTION_LOG_SERVER					7
#define DHCP_OPTION_COOKIE_SERVER				8
#define DHCP_OPTION_LPR_SERVER					9
#define DHCP_OPTION_IMPRESS_SERVER				10
#define DHCP_OPTION_RESOURCE_LOCATION			11
#define DHCP_OPTION_HOST_NAME					12
#define DHCP_OPTION_BOOT_SIZE					13
#define DHCP_OPTION_MERIT_DUMP_FILE				14
#define DHCP_OPTION_DOMAIN_NAME					15
#define DHCP_OPTION_SWAP_SERVER					16
#define DHCP_OPTION_ROOT_PATH					17
#define DHCP_OPTION_EXTENSIONS_PATH				18
#define DHCP_OPTION_IP_FORWARDING				19
#define DHCP_OPTION_NON_LOCAL_SOURCE_ROUTING	20
#define DHCP_OPTION_POLICY_FILTER				21
#define DHCP_OPTION_MAX_DATAGRAM_REASSEMBLY		22
#define DHCP_OPTION_DEFAULT_IP_TIME_TO_LIVE		23
#define DHCP_OPTION_PATH_MTU_AGING_TIMEOUT		24
#define DHCP_OPTION_PATH_MTU_PLATEAU_TABLE		25
#define DHCP_OPTION_INTERFACE_MTU				26
#define DHCP_OPTION_ALL_SUBNETS_ARE_LOCAL		27
#define DHCP_OPTION_BROADCAST_ADDRESS			28
#define DHCP_OPTION_PERFORM_MASK_DISCOVERY		29
#define DHCP_OPTION_MASK_SUPPLIER				30
#define DHCP_OPTION_PERFORM_ROUTER_DISCOVERY	31
#define DHCP_OPTION_ROUTER_SOLICITATION_ADDRESS 32
#define DHCP_OPTION_STATIC_ROUTE				33
#define DHCP_OPTION_TRAILER_ENCAPSULATION		34
#define DHCP_OPTION_ARP_CACHE_TIMEOUT			35
#define DHCP_OPTION_ETHERNET_ENCAPSULATION		36
#define DHCP_OPTION_TCP_DEFAULT_TTL				37
#define DHCP_OPTION_TCP_KEEPALIVE_INTERVAL		38
#define DHCP_OPTION_TCP_KEEPALIVE_GARBAGE		39
#define DHCP_OPTION_NIS_DOMAIN					40
#define DHCP_OPTION_NIS_SERVERS					41
#define DHCP_OPTION_NTP_SERVERS					42
#define DHCP_OPTION_VENDOR_SPECIFIC_INFO		43
#define DHCP_OPTION_NETBIOS_NAME_SERVER			44
#define DHCP_OPTION_NETBIOS_DATAGRAM_SERVER		45
#define DHCP_OPTION_NETBIOS_NODE_TYPE			46
#define DHCP_OPTION_NETBIOS_SCOPE				47
#define DHCP_OPTION_FONT_SERVERS				48
#define DHCP_OPTION_X_DISPLAY_MANAGER			49
#define DHCP_OPTION_REQUESTED_IP_ADDRESS		50
#define DHCP_OPTION_IP_ADDRESS_LEASE_TIME		51
#define DHCP_OPTION_OVERLOAD					52
#define DHCP_OPTION_MESSAGE_TYPE				53
#define DHCP_OPTION_SERVER_IDENTIFIER			54
#define DHCP_OPTION_PARAMETER_REQUEST_LIST		55
#define DHCP_OPTION_MESSAGE						56
#define DHCP_OPTION_MAX_SIZE					57
#define DHCP_OPTION_RENEWAL_TIME				58
#define DHCP_OPTION_REBINDING_TIME				59
#define DHCP_OPTION_VENDOR_CLASS_IDENTIFIER		60
#define DHCP_OPTION_CLIENT_IDENTIFIER			61
#define DHCP_OPTION_END							255

#define DHCP_MAGIC_COOKIE 0x63825363 // 99, 130, 83, 99

#define DHCP_DISCOVER 1
#define DHCP_OFFER	  2
#define DHCP_REQUEST  3
#define DHCP_DECLINE  4
#define DHCP_ACK	  5
#define DHCP_NAK	  6
#define DHCP_RELEASE  7
#define DHCP_INFORM	  8

#define DHCP_RETRANSMIT_DELAY(retry_times) \
	MIN(1 << (retry_times + 2), 64) * 1000

typedef enum {
	DOI_SUBNET_MASK,
	DOI_ROUTER,
	DOI_DNS,
	DOI_SERVER_ID,
	DOI_IP_LEASE_TIME,
	DOI_MESSAGE_TYPE,
	DOI_RENEWAL_TIME,
	DOI_REBINDING_TIME,
	DOI_MAX,
} DhcpOptionIndex;

/**
 * DHCP Client状态
 * 定义于RFC1541 Figure 5: State-transition diagram for DHCP client
 */
typedef enum {
	DHCP_STAT_BOUND,
	DHCP_STAT_INIT,
	DHCP_STAT_INIT_REBOOT,
	DHCP_STAT_REBINDING,
	DHCP_STAT_REBOOTING,
	DHCP_STAT_RENEWING,
	DHCP_STAT_REQUESTING,
	DHCP_STAT_SELECTING,
	DHCP_FAILED, // 非RFC中包含的状态，表示重传失败退出
} DhcpClientState;

typedef struct {
	NetworkDevice	  *device;
	DhcpClientState	   state;
	NetworkConnection *conn;
	Timer			   timeout_timer;
	Timer			   lease_timer;
	Timer			   rebind_timer;
	Timer			   renew_timer;
	uint32_t		   t0, t1, t2;

	uint32_t xid;
	uint8_t	 haddr_len;			 // 硬件地址长度
	uint8_t	 haddr_type;		 // 硬件地址类型
	uint8_t	 haddr[8];			 // 硬件地址
	uint8_t	 ip_addr[4];		 // IP地址
	uint8_t	 server_ip_addr[4];	 // 服务器IP地址
	uint8_t	 server_haddr[8];	 // 服务器硬件地址
	uint8_t	 gateway_ip_addr[4]; // 网关IP地址

	int retry_times; // 重传次数
} DhcpClient;

/**
 * DHCP报文头部结构
 * RFC 1541 Figure 1: Format of a DHCP message
 */
typedef struct {
	uint8_t	 op;		 // Message type
	uint8_t	 htype;		 // Hardware address type
	uint8_t	 hlen;		 // Hardware address length
	uint8_t	 hops;		 // Hops
	uint32_t xid;		 // Transaction ID
	uint16_t secs;		 // Seconds elapsed
	uint16_t flags;		 // Flags
	uint32_t ciaddr;	 // Client IP address
	uint32_t yiaddr;	 // Your IP address
	uint32_t siaddr;	 // Server IP address
	uint32_t giaddr;	 // Gateway IP address
	uint8_t	 chaddr[16]; // Client hardware address
	uint8_t	 sname[64];	 // Server name
	uint8_t	 file[128];	 // Boot file name
	uint8_t	 options[0]; // Options field
} DhcpHeader;

ProtocolResult dhcp_start(NetworkDevice *device);

#endif