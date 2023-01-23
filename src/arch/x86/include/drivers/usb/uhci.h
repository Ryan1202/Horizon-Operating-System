#ifndef _UHCI_H
#define _UHCI_H

#include <stdint.h>

#define UHCI_REG_CMD     0x00
#define UHCI_REG_STS     0x02
#define UHCI_REG_USBINTR 0x04
#define UHCI_FRNUM       0x06
#define UHCI_FRBASEADD   0x08
#define UHCI_SOFMOD      0x0c
#define UHCI_PORTSC1     0x10
#define UHCI_PORTSC2     0x12

#define UHCI_CMD_RUN        0x01
#define UHCI_CMD_HCRESET    0x02
#define UHCI_CMD_GLBRESET   0x04
#define UHCI_CMD_GLBSUSPEND 0x08
#define UHCI_CMD_GLBRESUME  0x10
#define UHCI_CMD_SWDBG      0x20
#define UHCI_CMD_CONFIGURE  0x40
#define UHCI_CMD_MAXPACKET  0x80 // 0=32bits,1=64bits

#define UHCI_INTR_CRC    0x01
#define UHCI_INTR_RESUME 0x02
#define UHCI_INTR_IOC    0x04
#define UHCI_INTR_SPI    0x08

struct uhci_frame_list {
    uint32_t *frames_vir;
    uint32_t *frames_phy;
};

struct uhci_qh {
    uint32_t qh_link;
    uint32_t qe_link;
};

struct uhci_td {
    uint32_t link;

    // TD control and status
    uint32_t actlen                : 11;
    uint32_t reserved1             : 5;
    uint32_t reserved2             : 1;
    uint32_t bitstuff_Error        : 1;
    uint32_t crc_timeout_Error     : 1;
    uint32_t NAK_received          : 1;
    uint32_t babble_detected       : 1;
    uint32_t databuffer_Error      : 1;
    uint32_t stalled               : 1;
    uint32_t active                : 1;
    uint32_t interrupt_on_complete : 1;
    uint32_t isochronous_select    : 1;
    uint32_t lowspeed_device       : 1;
    uint32_t error_count           : 2;
    uint32_t short_packet_detect   : 1;
    uint32_t reserved3             : 2;

    // TD token
    uint8_t  packet_id;
    uint32_t device_addr : 7;
    uint32_t endpoint    : 4;
    uint32_t data_toggle : 1;
    uint32_t reserved4   : 1;
    uint32_t max_length  : 11;

    uint32_t *buf;
    uint32_t  software_use[4];
};

struct uhci_skel {
    struct uhci_qh qh[12]; // 1ms, 2ms, 4ms, 8ms, 16ms, 32ms, 64ms, 128ms, 256ms, 512ms, 1024ms
};

enum uhci_gap {
    TIME_1MS = 0,
    TIME_2MS,
    TIME_4MS,
    TIME_8MS,
    TIME_16MS,
    TIME_32MS,
    TIME_64MS,
    TIME_128MS,
    TIME_256MS,
    TIME_512MS,
    TIME_1024MS
};

#endif