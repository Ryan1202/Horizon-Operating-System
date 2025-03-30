#include "sb16.h"
#include "driver/interrupt_dm.h"
#include "objects/object.h"
#include <bits.h>
#include <driver/sound/pcm.h>
#include <driver/sound/sound_dm.h>
#include <driver/timer_dm.h>
#include <drivers/bus/isa/dma.h>
#include <drivers/bus/isa/isa.h>
#include <drivers/bus/pci/pci.h>
#include <kernel/bus_driver.h>
#include <kernel/device.h>
#include <kernel/device_driver.h>
#include <kernel/driver.h>
#include <kernel/driver_dependency.h>
#include <kernel/driver_interface.h>
#include <kernel/func.h>
#include <kernel/initcall.h>
#include <kernel/memory.h>
#include <kernel/spinlock.h>
#include <stdint.h>
#include <string.h>

DriverResult sb16_init(struct Device *dev);
DriverResult sb16_probe(IsaDeviceDriver *isa_device_driver);

DriverResult sb16_pcm_set_default_params(PcmStream *stream);
DriverResult sb16_pcm_set_channel(PcmStream *stream, uint8_t channel);
DriverResult sb16_pcm_set_sample_rate(PcmDevice *pcm, uint32_t sample_rate);
DriverResult sb16_pcm_set_data_type(PcmStream *stream, PcmDataType type);
DriverResult sb16_play_open(SoundDevice *sound_device, PcmStream *stream);
DriverResult sb16_play_prepare(
	struct PcmStream *stream, void *addr, size_t size);
DriverResult sb16_play_trigger(PcmStream *stream, PcmTrigger trigger);
size_t		 sb16_play_position(PcmStream *stream);

DriverResult sb16_record_open(SoundDevice *sound_device, PcmStream *stream);
DriverResult sb16_record_prepare(
	struct PcmStream *stream, void *addr, size_t size);
DriverResult sb16_record_trigger(PcmStream *stream, PcmTrigger trigger);
size_t		 sb16_record_position(PcmStream *stream);

DeviceDriverOps sb16_device_driver_ops = {
	.device_driver_init	  = NULL,
	.device_driver_uninit = NULL,
};
DeviceOps sb16_device_ops = {
	.init	 = sb16_init,
	.start	 = NULL,
	.destroy = NULL,
	.status	 = NULL,
	.stop	 = NULL,
};
IsaOps sb16_isa_ops = {
	.probe = sb16_probe,
};
SoundDeviceOps sb16_sound_device_ops = {
	.set_volume = NULL,
};
PcmOps sb16_pcm_ops = {
	.set_sample_rate = sb16_pcm_set_sample_rate,
};
PcmStreamOps sb16_pcm_play_ops = {
	.open	  = sb16_play_open,
	.close	  = NULL,
	.prepare  = sb16_play_prepare,
	.trigger  = sb16_play_trigger,
	.position = sb16_play_position,

	.set_default_params = sb16_pcm_set_default_params,
	.set_data_type		= sb16_pcm_set_data_type,
	.set_channel		= sb16_pcm_set_channel,
};
PcmStreamOps sb16_pcm_record_ops = {
	.open	 = sb16_record_open,
	.close	 = NULL,
	.prepare = sb16_record_prepare,
	.trigger = sb16_record_trigger,

	.set_default_params = sb16_pcm_set_default_params,
	.set_data_type		= sb16_pcm_set_data_type,
	.set_channel		= sb16_pcm_set_channel,
};

DriverDependency sb_dependencies[] = {
	{
		.in_type		   = DRIVER_DEPENDENCY_TYPE_BUS,
		.dependency_in_bus = {BUS_TYPE_ISA, 0},
		.out_bus		   = NULL,
	 },
};

Driver sb_driver = {
	.short_name		  = STRING_INIT("HorizonSoundBlasterDriver"),
	.dependency_count = sizeof(sb_dependencies) / sizeof(DriverDependency),
	.dependencies	  = sb_dependencies,
};
DeviceDriver sb16_device_driver = {
	.name	  = STRING_INIT("SoundBlaster16"),
	.bus	  = NULL,
	.type	  = DEVICE_TYPE_SOUND,
	.priority = DRIVER_PRIORITY_BASIC,
	.ops	  = &sb16_device_driver_ops,
};
const Device sb16_device_template = {
	.name			   = STRING_INIT("sb16"),
	.device_driver	   = &sb16_device_driver,
	.ops			   = &sb16_device_ops,
	.private_data_size = 0,
};
const SoundDeviceCapabilities sb16_capabilities = {
	.record			 = 1,
	.play			 = 1,
	.set_volume		 = 1,
	.set_sample_rate = 1,
};
const SoundDevice sb16_sound_device_template = {
	.capabilities = sb16_capabilities,
	.ops		  = &sb16_sound_device_ops,
	.type		  = SOUND_TYPE_PCM,
};

static const int sb16_possible_ports[] = {0x220, 0x240, 0x260, 0x280};
static const int sb16_possible_dmas[]  = {5, 6, 7};
#define DMA_MAX 4
uint32_t data_len[DMA_MAX];

DriverResult sb16_write(Sb16Ports *ports, uint8_t value);

void sb16_irq_handler(Device *device) {
	SoundDevice *sound_device	= device->dm_ext;
	Sb16Info	*info			= sound_device->private_data;
	PcmDevice	*pcm			= sound_device->pcm;
	PcmStream	*current_stream = pcm->current_stream;

	sound_pcm_done(current_stream);

	if (info->major_ver >= 4) {
		io_in8(info->ports.iack16);
	} else {
		io_in8(info->ports.read_status);
	}
}

DriverResult sb16_reset(Sb16Ports *ports) {
	Timer timer;
	timer_init(&timer);
	io_out8(ports->reset, 1);
	delay_ms(&timer, 1);
	io_out8(ports->reset, 0);
	delay_ms(&timer, 1);
	for (int i = 0; i < 100000; i++) {
		if (io_in8(ports->read_status) & 0x80) {
			if (io_in8(ports->read) == 0xaa) { return DRIVER_RESULT_OK; }
		}
	}
	return DRIVER_RESULT_DEVICE_NOT_EXIST;
}

DriverResult sb16_write(Sb16Ports *ports, uint8_t value) {
	for (int i = 0; i < 100000; i++) {
		if ((io_in8(ports->write) & 0x80) == 0) {
			io_out8(ports->write, value);
			return DRIVER_RESULT_OK;
		}
	}
	return DRIVER_RESULT_TIMEOUT;
}

DriverResult sb16_read(Sb16Ports *ports, uint8_t *value) {
	for (int i = 0; i < 100000; i++) {
		if (io_in8(ports->iack16) & 0x80) {
			*value = io_in8(ports->read);
			return DRIVER_RESULT_OK;
		}
	}
	return DRIVER_RESULT_TIMEOUT;
}

DriverResult sb16_get_version(
	Sb16Ports *ports, uint8_t *major_version, uint8_t *minor_version) {
	sb16_write(ports, CMD_GET_DSP_VERSION);

	DRIVER_RESULT_PASS(sb16_read(ports, major_version));
	DRIVER_RESULT_PASS(sb16_read(ports, minor_version));

	return DRIVER_RESULT_OK;
}

DriverResult sb16_check(int port, Sb16Info **info) {
	Sb16Ports ports;
	ports.mixer		  = port + PORT_MIXER;
	ports.mixer_data  = port + PORT_MIXER_DATA;
	ports.reset		  = port + PORT_RESET;
	ports.read		  = port + PORT_READ;
	ports.write		  = port + PORT_WRITE;
	ports.read_status = port + PORT_READ_STATUS;
	ports.iack16	  = port + PORT_IACK16;

	uint8_t major, minor;
	DRIVER_RESULT_PASS(sb16_reset(&ports));
	DRIVER_RESULT_PASS(sb16_get_version(&ports, &major, &minor));
	print_driver_info(
		sb_driver, "SB16 DSP found. version:%d.%d\n", major, minor);

	*info			   = kmalloc(sizeof(Sb16Info));
	(*info)->ports	   = ports;
	(*info)->major_ver = major;
	(*info)->minor_ver = minor;

	return DRIVER_RESULT_OK;
}

DriverResult sb16_probe(IsaDeviceDriver *isa_device_driver) {
	for (int i = 0; i < sizeof(sb16_possible_ports) / sizeof(int); i++) {
		Sb16Info *info = NULL;
		if (sb16_check(sb16_possible_ports[i], &info) == DRIVER_RESULT_OK) {
			Device *sb16_device = kmalloc_from_template(sb16_device_template);
			SoundDevice *sb16_sound_device =
				kmalloc_from_template(sb16_sound_device_template);
			ObjectAttr attr			  = device_object_attr;
			sb16_device->bus		  = isa_device_driver->bus;
			sb16_device->private_data = info;
			DRIVER_RESULT_PASS(register_sound_device(
				&sb16_device_driver, sb16_device, sb16_sound_device, &attr));

			PcmDevice *pcm = sound_register_pcm(
				sb16_sound_device, &sb16_pcm_ops, 128 * 1024);
			if (pcm == NULL) return DRIVER_RESULT_OUT_OF_MEMORY;

			DRIVER_RESULT_PASS(pcm_register_stream(
				pcm, &pcm->play_stream, &sb16_pcm_play_ops,
				&info->stream_info[0]));
			DRIVER_RESULT_PASS(pcm_register_stream(
				pcm, &pcm->record_stream, &sb16_pcm_record_ops,
				&info->stream_info[1]));
			DRIVER_RESULT_PASS(pcm_register_dma(pcm, NULL, NULL, &isa_dma_ops));
		}
	}
	return DRIVER_RESULT_OK;
}

DriverResult sb16_init(struct Device *dev) {
	Sb16Info  *info = (Sb16Info *)dev->private_data;
	DeviceIrq *irq	= kmalloc(sizeof(DeviceIrq));
	// io_out8(info->ports.mixer, 0x80 /* 设置IRQ */);
	// io_out8(info->ports.mixer_data, 0x02 /* IRQ5 */);
	irq->irq		= 5;
	irq->device		= dev;
	irq->handler	= sb16_irq_handler;

	dev->irq = irq;
	register_device_irq(dev->irq);
	interrupt_enable_irq(irq->irq);

	spinlock_init(&info->lock);

	memset(data_len, 0, sizeof(data_len));
	return DRIVER_RESULT_OK;
}

DriverResult sb16_set_sample_rate(Sb16Info *info, uint16_t sample_rate) {
	sb16_write(&info->ports, CMD_SET_OUTPUT_SAMPLE_RATE);
	sb16_write(&info->ports, (uint8_t)(sample_rate >> 8));
	sb16_write(&info->ports, (uint8_t)sample_rate);
	return DRIVER_RESULT_OK;
}

DriverResult sb16_set_time_constant(Sb16Info *info, uint16_t sample_rate) {
	uint16_t time_constant = 65536 - (256000000 / sample_rate);
	sb16_write(&info->ports, CMD_SET_TIME_CONSTANT);
	sb16_write(&info->ports, (uint8_t)(time_constant >> 8));
	sb16_write(&info->ports, (uint8_t)time_constant);
	return DRIVER_RESULT_OK;
}

void sb16_setup_dma(
	Sb16Info *info, uint8_t channel, uint8_t mode, uint32_t addr,
	uint32_t len) {
	int flags = dma_lock();
	dma_disable(channel);
	dma_ff_reset(channel);
	dma_set_mode(channel, mode);
	dma_set_addr(channel, addr);
	dma_set_count(channel, len);
	dma_enable(channel);
	dma_unlock(flags);
}

DriverResult sb16_set_volume(Sb16Info *info, int volume) {
	int flags = spin_lock_irqsave(&info->lock);
	sb16_write(&info->ports, CMD_SET_OUTPUT_SAMPLE_RATE);
	sb16_write(&info->ports, (uint8_t)(volume >> 8));
	sb16_write(&info->ports, (uint8_t)volume);
	spin_unlock_irqrestore(&info->lock, flags);
	return DRIVER_RESULT_OK;
}

DriverResult sb16_pcm_set_default_params(PcmStream *stream) {
	stream->data_type = PCM_S16LE;
	sb16_pcm_set_data_type(stream, stream->data_type);
	stream->hw_mode			 = PCM_MODE_INTERLEAVED;
	stream->user_mode		 = PCM_MODE_INTERLEAVED;
	stream->channel			 = 2;
	stream->pcm->data_bytes	 = sizeof(uint16_t);
	stream->frame_bytes		 = sizeof(uint16_t) * stream->channel;
	stream->period_bytes	 = 16 * 1024;
	stream->frame_per_period = stream->period_bytes / stream->frame_bytes;
	stream->start_threshold	 = 4 * 1024;
	stream->stop_threshold	 = 1 * 1024;
	return DRIVER_RESULT_OK;
}

DriverResult sb16_pcm_set_data_type(PcmStream *stream, PcmDataType type) {
	struct Sb16StreamInfo *info = stream->private_data;
	if (type == PCM_U16LE) {
		info->data_type &= ~BIT(STEREO_BIT);
	} else if (type == PCM_S16LE) {
		info->data_type |= BIT(SIGNED_BIT);
	}
	return DRIVER_RESULT_OK;
}

DriverResult sb16_pcm_set_mode(PcmStream *stream, PcmMode mode) {
	if (mode == PCM_MODE_INTERLEAVED) stream->hw_mode = mode;
	else return DRIVER_RESULT_UNSUPPORT_FEATURE;
	return DRIVER_RESULT_OK;
}

DriverResult sb16_pcm_set_channel(PcmStream *stream, uint8_t channel) {
	struct Sb16StreamInfo *info = stream->private_data;
	if (channel == 1) {
		info->data_type &= ~BIT(STEREO_BIT);
	} else if (channel == 2) {
		info->data_type |= BIT(STEREO_BIT);
	}
	return DRIVER_RESULT_OK;
}

DriverResult sb16_pcm_set_sample_rate(PcmDevice *pcm, uint32_t sample_rate) {
	return sb16_set_sample_rate(pcm->sound_device->private_data, sample_rate);
}

DriverResult sb16_play_open(SoundDevice *sound_device, PcmStream *stream) {
	Sb16Info *info	  = (Sb16Info *)sound_device->device->private_data;
	info->dma_channel = dma_channel_use(
		sound_device->device, (int *)sb16_possible_dmas,
		sizeof(sb16_possible_dmas) / sizeof(int));

	return DRIVER_RESULT_OK;
}

DriverResult sb16_play_prepare(PcmStream *stream, void *addr, size_t size) {
	Sb16Info *info = (Sb16Info *)stream->sound_device->device->private_data;
	struct Sb16StreamInfo *stream_info = stream->private_data;

	sb16_setup_dma(
		info, info->dma_channel,
		DMA_MODE_SINGLE | DMA_MODE_AUTO | DMA_MODE_WRITE, (uint32_t)addr, size);

	uint16_t sample_count = stream->period_bytes >> 1;
	sample_count--;

	int flags = spin_lock_irqsave(&info->lock);
	sb16_write(
		&info->ports, TRANSFER_16BIT | TRANSFER_PLAY | TRANSFER_AUTOINIT);
	sb16_write(&info->ports, stream_info->data_type);
	sb16_write(&info->ports, (uint8_t)sample_count);
	sb16_write(&info->ports, (uint8_t)(sample_count >> 8));
	sb16_write(&info->ports, CMD_STOP_PLAY16);
	spin_unlock_irqrestore(&info->lock, flags);

	return DRIVER_RESULT_OK;
}

DriverResult sb16_play_trigger(PcmStream *stream, PcmTrigger trigger) {
	Sb16Info *info = (Sb16Info *)stream->sound_device->device->private_data;
	switch (trigger) {
	case PCM_TRIGGER_START:
	case PCM_TRIGGER_RESUME:
		sb16_write(&info->ports, CMD_RESUME_PLAY16);
		break;
	case PCM_TRIGGER_STOP:
	case PCM_TRIGGER_PAUSE:
		sb16_write(&info->ports, CMD_STOP_PLAY16);
		break;
	case PCM_TRIGGER_NONE:
		return DRIVER_RESULT_OTHER_ERROR;
	}
	return DRIVER_RESULT_OK;
}

size_t sb16_play_position(PcmStream *stream) {
	Sb16Info *info = (Sb16Info *)stream->sound_device->device->private_data;
	return dma_pointer(info->dma_channel, stream->pcm->buffer_bytes);
}

DriverResult sb16_record_open(SoundDevice *sound_device, PcmStream *stream) {
	Sb16Info *info	  = (Sb16Info *)sound_device->device->private_data;
	info->dma_channel = dma_channel_use(
		sound_device->device, (int *)sb16_possible_dmas,
		sizeof(sb16_possible_dmas) / sizeof(int));

	return DRIVER_RESULT_OK;
}

DriverResult sb16_record_prepare(PcmStream *stream, void *addr, size_t size) {
	Sb16Info *info = (Sb16Info *)stream->sound_device->device->private_data;
	struct Sb16StreamInfo *stream_info = stream->private_data;

	sb16_setup_dma(
		info, info->dma_channel,
		DMA_MODE_SINGLE | DMA_MODE_AUTO | DMA_MODE_READ, (uint32_t)addr, size);

	uint16_t sample_count = stream->period_bytes >> 1;
	sample_count--;

	int flags = spin_lock_irqsave(&info->lock);
	sb16_write(
		&info->ports, TRANSFER_16BIT | TRANSFER_RECORD | TRANSFER_AUTOINIT);
	sb16_write(&info->ports, stream_info->data_type);
	sb16_write(&info->ports, (uint8_t)sample_count);
	sb16_write(&info->ports, (uint8_t)(sample_count >> 8));
	sb16_write(&info->ports, CMD_STOP_PLAY16);
	spin_unlock_irqrestore(&info->lock, flags);

	return DRIVER_RESULT_OK;
}

DriverResult sb16_record_trigger(PcmStream *stream, PcmTrigger trigger) {
	Sb16Info *info = (Sb16Info *)stream->sound_device->device->private_data;
	switch (trigger) {
	case PCM_TRIGGER_START:
	case PCM_TRIGGER_RESUME:
		sb16_write(&info->ports, CMD_RESUME_PLAY16);
		break;
	case PCM_TRIGGER_STOP:
	case PCM_TRIGGER_PAUSE:
		sb16_write(&info->ports, CMD_STOP_PLAY16);
		break;
	case PCM_TRIGGER_NONE:
		return DRIVER_RESULT_OTHER_ERROR;
	}
	return DRIVER_RESULT_OK;
}

static void __init sb16_driver_entry(void) {
	register_driver(&sb_driver);
	register_device_driver(&sb_driver, &sb16_device_driver);
	isa_register_device_driver(&sb16_device_driver, &sb16_isa_ops);
}

driver_initcall(sb16_driver_entry);