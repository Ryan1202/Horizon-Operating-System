#ifndef _UHCI_H
#define _UHCI_H

#include <bits.h>
#include <drivers/pci.h>
#include <drivers/usb/usb.h>
#include <stdint.h>

#define FRAMELIST_SIZE 1024

#define UHCI_REG_CMD	 0x00
#define UHCI_REG_STS	 0x02
#define UHCI_REG_USBINTR 0x04
#define UHCI_FRNUM		 0x06
#define UHCI_FRBASEADD	 0x08
#define UHCI_SOFMOD		 0x0c
#define UHCI_PORTSC1	 0x10
#define UHCI_PORTSC2	 0x12

#define UHCI_CMD_RUN		0x01
#define UHCI_CMD_HCRESET	0x02
#define UHCI_CMD_GLBRESET	0x04
#define UHCI_CMD_GLBSUSPEND 0x08
#define UHCI_CMD_GLBRESUME	0x10
#define UHCI_CMD_SWDBG		0x20
#define UHCI_CMD_CONFIGURE	0x40
#define UHCI_CMD_MAXPACKET	0x80 // 0=32bits,1=64bits

#define UHCI_INTR_CRC	 0x01
#define UHCI_INTR_RESUME 0x02
#define UHCI_INTR_IOC	 0x04
#define UHCI_INTR_SPI	 0x08

#define UHCI_PORT_SC_SUSPEND   BIT(12)
#define UHCI_PORT_SC_RESET	   BIT(9)
#define UHCI_PORT_SC_LOWSPEED  BIT(8)
#define UHCI_PORT_SC_RESUME	   BIT(6)
#define UHCI_PORT_SC_EN_CHG	   BIT(3)
#define UHCI_PORT_SC_ENABLE	   BIT(2)
#define UHCI_PORT_SC_CONN_CHG  BIT(1)
#define UHCI_PORT_SC_CONNECTED BIT(0)

#define UHCI_STAT_SPEED			  BIT(29)
#define UHCI_STAT_ERROR_LIMIT_BIT 27
#define UHCI_STAT_LSD			  BIT(26)
#define UHCI_STAT_IOS			  BIT(25)
#define UHCI_STAT_IOC			  BIT(24)
#define UHCI_STAT_ACTIVE		  BIT(23)
#define UHCI_STAT_STALLED		  BIT(22)
#define UHCI_STAT_DATA_BUF_ERR	  BIT(21)
#define UHCI_STAT_BABBLE_DETCTED  BIT(20)
#define UHCI_STAT_NAK_RECIVED	  BIT(19)
#define UHCI_STAT_CRC_TIMEOUT_ERR BIT(18)
#define UHCI_STAT_BITSTUFF_ERR	  BIT(17)
#define UHCI_STAT_ACTLEN_BIT	  0

#define UHCI_TOKEN_MAXLEN_BIT	   21
#define UHCI_TOKEN_DATA_TOGGLE	   BIT(19)
#define UHCI_TOKEN_ENDPOINT_BIT	   15
#define UHCI_TOKEN_DEVICE_ADDR_BIT 8
#define UHCI_TOKEN_PACKET_ID_BIT   0

#define UHCI_VERTICAL_FIRST BIT(2)
#define UHCI_QH_TD_SELECT	BIT(1) // 1:QH, 0:TD
#define UHCI_TERMINATE		BIT(0)

struct uhci_frame_list {
	uint32_t *frames_vir;
	uint32_t *frames_phy;
};

typedef struct uhci_qh {
	// 硬件用
	uint32_t qh_link;
	uint32_t qe_link;
	// 软件用
	uint32_t qh_addr_phy;
	uint32_t prev_ptr;
	uint32_t last_ptr;
	uint32_t next_ptr;
	uint32_t align[2]; // 用于对齐16字节
} uhci_qh_t;

typedef struct uhci_td {
	uint32_t link;

	// TD control and status
	uint32_t actlen				   : 11;
	uint32_t reserved1			   : 5;
	uint32_t reserved2			   : 1;
	uint32_t bitstuff_Error		   : 1;
	uint32_t crc_timeout_Error	   : 1;
	uint32_t NAK_received		   : 1;
	uint32_t babble_detected	   : 1;
	uint32_t databuffer_Error	   : 1;
	uint32_t stalled			   : 1;
	uint32_t active				   : 1;
	uint32_t interrupt_on_complete : 1;
	uint32_t isochronous_select	   : 1;
	uint32_t lowspeed_device	   : 1;
	uint32_t error_count		   : 2;
	uint32_t short_packet_detect   : 1;
	uint32_t reserved3			   : 2;

	// TD token
	uint8_t	 packet_id;
	uint32_t device_addr : 7;
	uint32_t endpoint	 : 4;
	uint32_t data_toggle : 1;
	uint32_t reserved4	 : 1;
	uint32_t max_length	 : 11;

	uint32_t buf_addr_phy;

	// software use
	uint32_t prev_ptr;
	uint32_t td_addr_phy;
	uint32_t software_use[2];
} __attribute__((packed)) uhci_td_t;

struct uhci_skel {
	struct uhci_qh qh[11]; // 1ms, 2ms, 4ms, 8ms, 16ms, 32ms, 64ms, 128ms
};

typedef struct {
	struct pci_device	  *device;
	uint32_t			   io_base;
	struct uhci_frame_list fl;
	uint8_t				   port_cnt;

	struct uhci_skel *skel;
} uhci_t;

enum uhci_skel_type {
	TIME_1MS = 0,
	TIME_2MS,
	TIME_4MS,
	TIME_8MS,
	TIME_16MS,
	TIME_32MS,
	TIME_64MS,
	TIME_128MS,
	LOW_SPEED,
	FULL_SPEED,
	TERM,
};

void uhci_skel_init(uhci_t *devext);

void uhci_skel_add_qh(uhci_t *devext, uhci_qh_t *qh, enum uhci_skel_type type);
void uhci_skel_del_qh(uhci_t *devext, uhci_qh_t *qh, enum uhci_skel_type type);

usb_setup_status_t uhci_control_transaction_in(
	usb_hcd_t *hcd, usb_device_t *device, usb_transfer_t *transfer,
	void *buffer, uint32_t data_length, usb_request_t *usb_req);
usb_setup_status_t uhci_control_transaction_out(
	usb_hcd_t *hcd, usb_device_t *device, usb_transfer_t *transfer,
	void *buffer, uint32_t data_length, usb_request_t *usb_req);

#endif