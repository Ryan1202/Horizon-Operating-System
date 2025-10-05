#ifndef _PCM_H
#define _PCM_H

#include <kernel/device.h>
#include <kernel/dma.h>
#include <kernel/driver.h>
#include <kernel/spinlock.h>
#include <kernel/wait_queue.h>
#include <multiple_return.h>
#include <objects/object.h>
#include <stdint.h>

#define PCM_STATUS_FREE			0
#define PCM_STATUS_HOST_USING	1
#define PCM_STATUS_DEVICE_USING 2

typedef enum PcmDataType {
	PCM_U8,
	PCM_S8,
	PCM_U16LE,
	PCM_U16BE,
	PCM_S16LE,
	PCM_S16BE,
	PCM_U24LE,
	PCM_U24BE,
	PCM_S24LE,
	PCM_S24BE,
	PCM_U32LE,
	PCM_U32BE,
	PCM_S32LE,
	PCM_S32BE,
	PCM_F32LE,
	PCM_F32BE,
	PCM_F64LE,
	PCM_F64BE,
} PcmDataType;

typedef enum PcmTrigger {
	PCM_TRIGGER_NONE,
	PCM_TRIGGER_START,
	PCM_TRIGGER_STOP,
	PCM_TRIGGER_PAUSE,
	PCM_TRIGGER_RESUME,
} PcmTrigger;

typedef enum PcmMode {
	PCM_MODE_INTERLEAVED,
	PCM_MODE_NONINTERLEAVED,
} PcmMode;

typedef enum PcmStatus {
	PCM_STATUS_CLOSE,
	PCM_STATUS_OPEN,
	PCM_STATUS_PREPARED,
	PCM_STATUS_RUNNING,
	PCM_STATUS_PAUSED,
} PcmStatus;

struct SoundDevice;
struct PcmStream;
typedef struct PcmStreamOps {
	DriverResult (*open)(
		struct SoundDevice *sound_device, struct PcmStream *stream);
	DriverResult (*close)(
		struct SoundDevice *sound_device, struct PcmStream *stream);
	DriverResult (*prepare)(
		struct PcmStream *pcm_stream, void *addr, size_t size);
	DriverResult (*trigger)(struct PcmStream *stream, PcmTrigger trigger);
	size_t (*position)(struct PcmStream *stream);

	DriverResult (*set_mode)(struct PcmStream *stream, PcmMode mode);
	DriverResult (*set_default_params)(struct PcmStream *stream);
	DriverResult (*set_data_type)(
		struct PcmStream *stream, PcmDataType data_type);
	DriverResult (*set_channel)(struct PcmStream *stream, uint8_t channel);
} PcmStreamOps;

typedef struct PcmStream {
	struct PcmDevice   *pcm;
	struct SoundDevice *sound_device;
	PcmStreamOps	   *ops;

	PcmDataType data_type;
	uint8_t		channel;

	spinlock_t lock;
	WaitQueue  wq;

	uint16_t frame_bytes; // 一个帧的大小
	size_t	 frame_per_period;
	uint32_t period_bytes; // 一个周期的大小

	uint32_t start_threshold;
	uint32_t stop_threshold;

	size_t device_ptr_base, device_period_ptr;
	size_t host_ptr_base, host_period_ptr;
	size_t host_ptr;

	PcmMode hw_mode, user_mode;

	void *private_data;
} PcmStream;

typedef struct PcmOps {
	DriverResult (*set_sample_rate)(
		struct PcmDevice *pcm, uint32_t sample_rate);
} PcmOps;

typedef struct PcmDevice {
	struct SoundDevice *sound_device;

	PcmStatus status;

	PcmStream *play_stream;
	PcmStream *record_stream;
	PcmStream *current_stream;

	void *buf;

	size_t	boundary;
	size_t	buffer_bytes;
	uint8_t data_bytes; // 数据单位大小

	uint32_t sample_rate;
	Dma		*dma;

	PcmOps *ops;
} PcmDevice;

PcmDevice *sound_register_pcm(
	struct SoundDevice *sound_device, PcmOps *pcm_ops, size_t size);
DriverResult pcm_register_stream(
	PcmDevice *pcm, PcmStream **stream, PcmStreamOps *ops, void *private_data);
DriverResult pcm_register_dma(
	PcmDevice *pcm, void *dma, void *param, DmaOps *ops);
DriverResult sound_pcm_alloc(PcmStream *stream);

DriverResult pcm_set_data_type(PcmStream *stream, PcmDataType data_type);
DriverResult pcm_set_channel(PcmStream *stream, uint8_t channel);
DriverResult pcm_set_sample_rate(PcmDevice *pcm, uint32_t sample_rate);
DriverResult sound_pcm_open(
	Object *object, int mode, DEF_MRET(PcmDevice *, pcm),
	DEF_MRET(PcmStream *, stream));
DriverResult sound_pcm_set_frame_count(PcmStream *stream, size_t count);
DriverResult sound_pcm_read(
	PcmStream *stream, uint8_t *buf, uint32_t frame_count);
DriverResult sound_pcm_write(
	PcmStream *stream, uint8_t *buf, uint32_t frame_count);
DriverResult sound_pcm_prepare(PcmStream *stream);
DriverResult sound_pcm_done(PcmStream *stream);

#endif