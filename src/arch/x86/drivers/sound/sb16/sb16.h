#ifndef _SB16_H
#define _SB16_H

#include "kernel/spinlock.h"
#include "stdint.h"

#define PORT_MIXER		 0x4
#define PORT_MIXER_DATA	 0x5
#define PORT_RESET		 0x6
#define PORT_READ		 0xa
#define PORT_WRITE		 0xc
#define PORT_READ_STATUS 0xe
#define PORT_IACK16		 0xf

#define CMD_SET_TIME_CONSTANT	   0x40
#define CMD_SET_OUTPUT_SAMPLE_RATE 0x41
#define CMD_TURN_SPEAKER_ON		   0xd1
#define CMD_TURN_SPEAKER_OFF	   0xd3
#define CMD_STOP_PLAY8			   0xd0
#define CMD_RESUME_PLAY8		   0xd4
#define CMD_STOP_PLAY16			   0xd5
#define CMD_RESUME_PLAY16		   0xd6
#define CMD_GET_DSP_VERSION		   0xe1

#define CMD_SET_VOLUME 0x22
#define CMD_SET_IRQ	   0x80

#define TRANSFER_16BIT	  0xb0
#define TRANSFER_8BIT	  0xc0
#define TRANSFER_PLAY	  0x00
#define TRANSFER_RECORD	  0x08
#define TRANSFER_FIFO	  0x02
#define TRANSFER_AUTOINIT 0x04

#define STEREO_BIT 5
#define SIGNED_BIT 4

typedef struct Sb16Ports {
	int mixer;
	int mixer_data;
	int reset;
	int read;
	int write;
	int read_status;
	int iack16;
} Sb16Ports;

#define SB16_MAX_DMA_REGION 4

typedef struct Sb16Info {
	Sb16Ports  ports;
	uint8_t	   major_ver;
	uint8_t	   minor_ver;
	uint8_t	   dma_channel;
	spinlock_t lock;

	struct Sb16StreamInfo {
		uint8_t data_type;
	} stream_info[2];
} Sb16Info;

#endif