#include "kernel/console.h"
#include "kernel/spinlock.h"
#include "kernel/thread.h"
#include "kernel/wait_queue.h"
#include "stdint.h"
#include <driver/sound/pcm.h>
#include <driver/sound/sound_dm.h>
#include <kernel/dma.h>
#include <kernel/driver.h>
#include <kernel/memory.h>
#include <math.h>
#include <multiple_return.h>
#include <objects/handle.h>
#include <objects/object.h>
#include <objects/transfer.h>
#include <string.h>

TransferResult sound_pcm_transfer(
	Object *object, struct ObjectHandle *handle, TransferDirection direction,
	uint8_t *buf, size_t size);

PcmDevice *sound_register_pcm(
	SoundDevice *sound_device, PcmOps *pcm_ops, size_t size) {
	PcmDevice *pcm = kmalloc(sizeof(PcmDevice));
	if (pcm == NULL) return NULL;

	sound_device->pcm = pcm;
	pcm->sound_device = sound_device;
	pcm->ops		  = pcm_ops;
	pcm->buf		  = NULL;
	pcm->buffer_bytes = size;
	pcm->status		  = PCM_STATUS_CLOSE;
	pcm->boundary	  = size;
	while (pcm->boundary * 2 < __SIZE_MAX__ / 2 - size)
		pcm->boundary *= 2;

	return pcm;
}

DriverResult pcm_register_stream(
	PcmDevice *pcm, PcmStream **stream, PcmStreamOps *ops, void *private_data) {
	*stream = kmalloc(sizeof(PcmStream));
	if (*stream == NULL) return DRIVER_RESULT_OUT_OF_MEMORY;
	PcmStream *s = *stream;

	s->pcm				 = pcm;
	s->sound_device		 = pcm->sound_device;
	s->ops				 = ops;
	s->private_data		 = private_data;
	// s->buf			   = ptr;
	// s->status		   = ptr + sizeof(void *) * pcm->max_buf_count;
	s->device_ptr_base	 = 0;
	s->device_period_ptr = 0;
	s->host_ptr_base	 = 0;
	s->host_period_ptr	 = 0;
	s->host_ptr			 = 0;
	spinlock_init(&s->lock);
	wait_queue_init(&s->wq);

	return DRIVER_RESULT_OK;
}

DriverResult pcm_register_dma(
	PcmDevice *pcm, void *dma, void *param, DmaOps *ops) {
	Dma *_dma = kmalloc(sizeof(Dma));
	if (_dma == NULL) return DRIVER_RESULT_OUT_OF_MEMORY;
	_dma->dma	= dma;
	_dma->param = param;
	_dma->ops	= ops;
	pcm->dma	= _dma;
	return DRIVER_RESULT_OK;
}

DriverResult sound_pcm_alloc(PcmStream *stream) {
	PcmDevice *pcm = stream->pcm;
	pcm->buf	   = pcm->dma->ops->dma_alloc(pcm->dma, pcm->buffer_bytes);
	stream->device_ptr_base = stream->host_ptr_base = 0;
	stream->device_period_ptr = stream->host_period_ptr = 0;
	stream->host_ptr									= 0;
	return DRIVER_RESULT_OK;
}

DriverResult pcm_set_data_type(PcmStream *stream, PcmDataType data_type) {
	DRIVER_RESULT_PASS(stream->ops->set_data_type(stream, data_type));
	stream->data_type = data_type;
	return DRIVER_RESULT_OK;
}

DriverResult pcm_set_channel(PcmStream *stream, uint8_t channel) {
	DRIVER_RESULT_PASS(stream->ops->set_channel(stream, channel));
	stream->channel = channel;
	return DRIVER_RESULT_OK;
}

DriverResult pcm_set_sample_rate(PcmDevice *pcm, uint32_t sample_rate) {
	DRIVER_RESULT_PASS(pcm->ops->set_sample_rate(pcm, sample_rate));
	pcm->sample_rate = sample_rate;
	return DRIVER_RESULT_OK;
}

DriverResult sound_pcm_open(
	Object *object, int mode, DEF_MRET(PcmDevice *, pcm),
	DEF_MRET(PcmStream *, stream)) {
	SoundDevice *sound_device = sound_get_device(object);
	PcmStream	*cur_stream;

	if (sound_device->type != SOUND_TYPE_PCM)
		return DRIVER_RESULT_UNSUPPORT_FEATURE;

	PcmDevice *pcm = sound_device->pcm;
	if (mode == SOUND_DEVICE_MODE_PLAY) {
		pcm->play_stream->ops->open(sound_device, pcm->play_stream);
		cur_stream = pcm->play_stream;
	} else if (mode == SOUND_DEVICE_MODE_RECORED) {
		pcm->record_stream->ops->open(sound_device, pcm->record_stream);
		cur_stream = pcm->record_stream;
	} else {
		return DRIVER_RESULT_UNSUPPORT_FEATURE;
	}
	cur_stream->ops->set_default_params(cur_stream);

	MRET(pcm)		   = pcm;
	MRET(stream)	   = cur_stream;
	object->in.type	   = TRANSFER_TYPE_STREAM;
	object->in.stream  = sound_pcm_transfer;
	object->out.type   = TRANSFER_TYPE_STREAM;
	object->out.stream = sound_pcm_transfer;
	return DRIVER_RESULT_OK;
}

DriverResult sound_pcm_set_frame_count(PcmStream *stream, size_t count) {
	stream->frame_per_period = count;
	stream->frame_bytes		 = stream->channel * stream->pcm->data_bytes;
	stream->period_bytes	 = stream->frame_per_period * stream->frame_bytes;
	return DRIVER_RESULT_OK;
}

DriverResult sound_pcm_set_mode(PcmStream *stream, PcmMode mode) {
	stream->user_mode = mode;
	return stream->ops->set_mode(stream, mode);
}

int sound_pcm_left_space(PcmStream *stream) {
	int left_space = stream->device_period_ptr + stream->pcm->buffer_bytes -
					 (stream->host_ptr_base + stream->host_ptr);
	if (left_space >= stream->pcm->boundary) {
		// 跨越边界
		left_space -= stream->pcm->boundary;
	}
	return left_space;
}

DriverResult sound_pcm_read(
	PcmStream *stream, uint8_t *buf, uint32_t frame_count) {
	// if (size > stream->pcm->max_size) return DRIVER_RESULT_EXCEED_MAX_SIZE;
	// memcpy(buf, stream->buf, size);
	return DRIVER_RESULT_OK;
}

void pcm_interleaved2noninterleaved(
	PcmStream *stream, uint8_t *in, uint8_t *out, uint32_t frame_count) {
	PcmDevice *pcm = stream->pcm;
	uint8_t	  *src = in;
	uint8_t	  *dst = out;
	uint8_t	  *_dst;
	uint32_t   offset = stream->period_bytes / stream->channel;
	int		   i, j, k;
	for (i = 0; i < frame_count; i++) {
		_dst = dst;
		for (j = 0; j < stream->channel; j++) {
			for (k = 0; k < pcm->data_bytes; k++) {
				_dst[k] = src[k];
			}
			_dst += offset;
			src += pcm->data_bytes;
		}
		dst += pcm->data_bytes;
	}
}
void pcm_noninterleaved2interleaved(
	PcmStream *stream, uint8_t *in, uint8_t *out, uint32_t frame_count) {
	PcmDevice *pcm = stream->pcm;
	uint8_t	  *src = in;
	uint8_t	  *dst = out;
	uint8_t	  *_src;
	uint32_t   offset = stream->period_bytes / stream->channel;
	int		   i, j, k;
	for (i = 0; i < frame_count; i++) {
		_src = src;
		for (j = 0; j < stream->channel; j++) {
			for (k = 0; k < pcm->data_bytes; k++) {
				dst[k] = _src[k];
			}
			_src += offset;
			dst += pcm->data_bytes;
		}
		src += pcm->data_bytes;
	}
}

DriverResult sound_pcm_write_interleaved(
	PcmStream *stream, uint8_t *dst, uint8_t *src, uint32_t frame_count) {
	if (frame_count > stream->frame_per_period)
		return DRIVER_RESULT_EXCEED_MAX_SIZE;
	if (stream->hw_mode == PCM_MODE_INTERLEAVED) {
		// 模式相同，直接复制
		memcpy(dst, src, frame_count * stream->frame_bytes);
	} else {
		// 模式不同，需要转换
		// 输入为交织模式，输出为非交织模式
		pcm_interleaved2noninterleaved(stream, src, dst, frame_count);
	}
	return DRIVER_RESULT_OK;
}

DriverResult sound_pcm_write_noninterleaved(
	PcmStream *stream, uint8_t *dst, uint8_t *src, uint32_t frame_count) {
	if (stream->hw_mode == PCM_MODE_NONINTERLEAVED) {
		// 模式相同，直接复制
		memcpy(dst, src, frame_count * stream->frame_bytes);
	} else {
		// 模式不同，需要转换
		// 输入为非交织模式，输出为交织模式
		pcm_noninterleaved2interleaved(stream, src, dst, frame_count);
	}
	return DRIVER_RESULT_OK;
}

DriverResult sound_pcm_write(
	PcmStream *stream, uint8_t *buf, uint32_t frame_count) {
	PcmDevice *pcm = stream->pcm;
	uint32_t   count;
	uint8_t	  *dst, *src = buf;

	int flags	   = spin_lock_irqsave(&stream->lock);
	int left_space = sound_pcm_left_space(stream);

	size_t writed = 0;
	size_t size, left_size = frame_count * stream->frame_bytes;
	while (left_size > 0) {
		while (left_space == 0) {
			wait_queue_add(&stream->wq);
			thread_set_status(TASK_INTERRUPTIBLE);
			spin_unlock_irqrestore(&stream->lock, flags);

			schedule();

			spin_lock_irqsave(&stream->lock);
			left_space = sound_pcm_left_space(stream);
		}
		dst	 = pcm->buf + stream->host_ptr;
		size = MIN(left_space, left_size);
		size = MIN(size, pcm->buffer_bytes - stream->host_ptr);
		spin_unlock_irqrestore(&stream->lock, flags);

		count = size / stream->frame_bytes;
		if (stream->user_mode == PCM_MODE_INTERLEAVED) {
			sound_pcm_write_interleaved(stream, dst, src, count);
		} else {
			sound_pcm_write_noninterleaved(stream, dst, src, count);
		}
		src += size;
		left_space -= size;
		left_size -= size;
		writed += count;

		flags = spin_lock_irqsave(&stream->lock);
		stream->host_ptr += size;

		if (stream->host_ptr >= pcm->buffer_bytes) {
			stream->host_ptr -= pcm->buffer_bytes;
			stream->host_ptr_base += pcm->buffer_bytes;
			if (stream->host_ptr_base > pcm->boundary) {
				stream->host_ptr_base = 0;
			}
		}
	}
	if (writed > 0) {
		if ((pcm->status == PCM_STATUS_PREPARED ||
			 pcm->status == PCM_STATUS_PAUSED)) {
			if (sound_pcm_left_space(stream) >= stream->start_threshold) {
				stream->ops->trigger(stream, PCM_TRIGGER_START);
				pcm->status = PCM_STATUS_RUNNING;
			}
		}
		int position = stream->host_ptr;
		position -= position % stream->period_bytes;
		stream->host_period_ptr = stream->host_ptr_base + position;
	}
	spin_unlock_irqrestore(&stream->lock, flags);
	if (writed < frame_count) { return DRIVER_RESULT_BUSY; };

	return DRIVER_RESULT_OK;
}

DriverResult sound_pcm_prepare(PcmStream *stream) {
	PcmDevice *pcm		= stream->pcm;
	pcm->current_stream = stream;

	pcm->status = PCM_STATUS_PREPARED;

	DRIVER_RESULT_PASS(
		stream->ops->prepare(stream, pcm->buf, pcm->buffer_bytes));

	return DRIVER_RESULT_OK;
}

DriverResult sound_pcm_done(PcmStream *stream) {
	PcmDevice *pcm			  = stream->pcm;
	size_t	   position		  = stream->ops->position(stream);
	size_t	   old_period_ptr = stream->device_period_ptr;
	position -= position % stream->period_bytes;

	int flags = spin_lock_irqsave(&stream->lock);

	size_t new_device_base = stream->device_ptr_base;
	size_t new_period_ptr  = new_device_base + position;

	if (new_period_ptr < old_period_ptr) {
		// 当前DMA缓冲区发生回环，切换到下一个虚拟缓冲区
		new_device_base += pcm->buffer_bytes;
		new_period_ptr = new_device_base + position;
	}

	if (new_period_ptr >= pcm->boundary) {
		// 当前虚拟缓冲区超出边界，切换到第一个虚拟缓冲区
		new_device_base = 0;
		new_period_ptr	= new_device_base + position;
	}

	stream->device_ptr_base	  = new_device_base;
	stream->device_period_ptr = new_period_ptr;

	// if (sound_pcm_left_space(stream) <= stream->stop_threshold) {
	// 	stream->ops->trigger(stream, PCM_TRIGGER_PAUSE);
	// 	stream->pcm->status = PCM_STATUS_PAUSED;
	// }

	wait_queue_wakeup(&stream->wq);
	spin_unlock_irqrestore(&stream->lock, flags);

	return DRIVER_RESULT_OK;
}

TransferResult sound_pcm_transfer(
	Object *object, struct ObjectHandle *handle, TransferDirection direction,
	uint8_t *buf, size_t size) {
	SoundDevice *sound_device = sound_get_device(object);
	PcmDevice	*pcm		  = sound_device->pcm;
	if (direction == TRANSFER_OUT) {
		sound_pcm_write(pcm->play_stream, buf, size);
	} else {
		sound_pcm_read(pcm->record_stream, buf, size);
	}

	return TRANSFER_OK;
}
