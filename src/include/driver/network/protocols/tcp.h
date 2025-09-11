#ifndef _TCP_H
#define _TCP_H

#include "driver/network/conn.h"
#include <driver/timer_dm.h>
#include <stdint.h>

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20

#define TCP_OPTION_END 0
#define TCP_OPTION_NOP 1
#define TCP_OPTION_MSS 2

#define TCP_SHUT_WR	  0
#define TCP_SHUT_RD	  1
#define TCP_SHUT_RDWR 2

#define TCP_MSL 120 * 1000

typedef struct {
	uint16_t src_port;
	uint16_t dest_port;
	uint32_t seq;
	uint32_t ack;
	uint8_t	 data_offset;
	uint8_t	 flags;
	uint16_t window_size;
	uint16_t checksum;
	uint16_t urgent_pointer;
	uint8_t	 options[0];
} TcpHeader;

//                               +---------+ ---------\      active OPEN
//                               |  CLOSED |            \    -----------
//                               +---------+<---------\   \   create TCB
//                                 |     ^              \   \  snd SYN
//                    passive OPEN |     |   CLOSE        \   \.
//                    ------------ |     | ----------       \   \.
//                     create TCB  |     | delete TCB         \   \.
//                                 V     |                      \   \.
//                               +---------+            CLOSE    |    \.
//                               |  LISTEN |          ---------- |     |
//                               +---------+          delete TCB |     |
//                    rcv SYN      |     |     SEND              |     |
//                   -----------   |     |    -------            |     V
//  +---------+      snd SYN,ACK  /       \   snd SYN          +---------+
//  |         |<-----------------           ------------------>|         |
//  |   SYN   |                    rcv SYN                     |   SYN   |
//  |   RCVD  |<-----------------------------------------------|   SENT  |
//  |         |                    snd ACK                     |         |
//  |         |------------------           -------------------|         |
//  +---------+   rcv ACK of SYN  \       /  rcv SYN,ACK       +---------+
//    |           --------------   |     |   -----------
//    |                  x         |     |     snd ACK
//    |                            V     V
//    |  CLOSE                   +---------+
//    | -------                  |  ESTAB  |
//    | snd FIN                  +---------+
//    |                   CLOSE    |     |    rcv FIN
//    V                  -------   |     |    -------
//  +---------+          snd FIN  /       \   snd ACK          +---------+
//  |  FIN    |<-----------------           ------------------>|  CLOSE  |
//  | WAIT-1  |------------------                              |   WAIT  |
//  +---------+          rcv FIN  \                            +---------+
//    | rcv ACK of FIN   -------   |                            CLOSE  |
//    | --------------   snd ACK   |                           ------- |
//    V        x                   V                           snd FIN V
//  +---------+                  +---------+                   +---------+
//  |FINWAIT-2|                  | CLOSING |                   | LAST-ACK|
//  +---------+                  +---------+                   +---------+
//    |                rcv ACK of FIN |                 rcv ACK of FIN |
//    |  rcv FIN       -------------- |    Timeout=2MSL -------------- |
//    |  -------              x       V    ------------        x       V
//     \ snd ACK                 +---------+delete TCB         +---------+
//      ------------------------>|TIME WAIT|------------------>| CLOSED  |
//                               +---------+                   +---------+
typedef enum {
	TCP_STATE_LISTEN,
	TCP_STATE_SYN_SENT,
	TCP_STATE_SYN_RECEIVED,
	TCP_STATE_ESTABLISHED,
	TCP_STATE_FIN_WAIT1,
	TCP_STATE_FIN_WAIT2,
	TCP_STATE_CLOSE_WAIT,
	TCP_STATE_CLOSING,
	TCP_STATE_LAST_ACK,
	TCP_STATE_TIME_WAIT,
	TCP_STATE_CLOSED,
} TcpState;

typedef struct Tcp {
	NetworkConnection *conn;
	TcpState		   state;
	TcpHeader		  *header;
	int				   header_size;
	int				   mss;

	struct task_s *thread;

	// Send Sequence Space
	//      1         2          3          4
	// ----------|----------|----------|----------
	//        SND.UNA    SND.NXT    SND.UNA
	//                             +SND.WND

	// 1 - old sequence numbers which have been acknowledged
	// 2 - sequence numbers of unacknowledged data
	// 3 - sequence numbers allowed for new data transmission
	// 4 - future sequence numbers which are not yet allowed
	struct {
		uint32_t unack;	 // send unacknowledged
		uint32_t next;	 // send next
		uint32_t window; // send window
		uint32_t urg_p;	 // send urgent pointer
		uint32_t wl1;	 // segment sequence number used for last window update
		uint32_t
			wl2; // segment acknowledgment number used for last window update
		uint32_t iss; // initial send sequence number
		uint32_t total_size;

		uint32_t remote_window_size;
	} send;
	uint8_t *send_window;
	// Receive Sequence Space
	//     1          2          3
	// ----------|----------|----------
	//        RCV.NXT    RCV.NXT
	//                  +RCV.WND

	// 1 - old sequence numbers which have been acknowledged
	// 2 - sequence numbers allowed for new reception
	// 3 - future sequence numbers which are not yet allowed
	struct {
		uint32_t next;	 // receive next
		uint32_t window; // receive window
		uint32_t urg_p;	 // receive urgent pointer
		uint32_t irs;	 // initial receive sequence number
		uint32_t total_size;

		uint32_t read;
	} recv;
	uint8_t *recv_window;

	struct {
		uint32_t seq;	 // segment sequence number
		uint32_t ack;	 // segment acknowledgment number
		uint32_t len;	 // segment length
		uint32_t window; // segment window
		uint32_t urg_p;	 // segment urgent pointer
		uint32_t prc;	 // segment precedence value
	} cur;

	uint32_t send_time, send_seq;
	int		 rttvar, srtt, rto;
	int		 retry_times;
	Timer	 timeout_timer;
} Tcp;

void		   tcp_register(NetworkConnection *conn);
ProtocolResult tcp_bind(NetworkConnection *conn, uint16_t port);
ProtocolResult tcp_connect(
	NetworkConnection *conn, uint8_t *dst_ip, uint16_t dst_port);
ProtocolResult tcp_send_data(NetworkConnection *conn, uint8_t *buf, int length);
void		   tcp_reset(NetworkConnection *conn);
ProtocolResult tcp_shutdown(NetworkConnection *conn, uint8_t how);
ProtocolResult tcp_recv(
	NetBuffer *net_buffer, int length, uint8_t *src_ip, uint8_t *dst_ip,
	int ip_len);

#endif