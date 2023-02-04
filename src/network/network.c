#include <fs/fs.h>
#include <fs/vfs.h>
#include <kernel/console.h>
#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/thread.h>
#include <math.h>
#include <network/network.h>

device_t *default_net_device;

void eth_dm_start();
void eth_device_register(device_t *device, char *name);
void eth_device_unregister(device_t *device);

driver_manager_t eth_dm = {
	.name		   = {0, 0, NULL},
	.private_data  = NULL,
	.dm_start	   = eth_dm_start,
	.dm_register   = eth_device_register,
	.dm_unregister = eth_device_unregister,
};

struct file_operations eth_fops = {
	.open  = dev_open,
	.close = dev_close,
	.read  = dev_read,
	.write = dev_write,
	.ioctl = dev_ioctl,
	.seek  = fs_seek,
};

typedef struct {
	uint8_t count;
	list_t *lists;
} eth_t;

void init_network(void) {
	eth_t *eth;
	eth_dm.private_data = kmalloc(sizeof(eth_t));
	eth					= eth_dm.private_data;
	eth->count			= 0;
	default_net_device	= NULL;
	list_init(&eth_dm.dev_listhead);
}

uint8_t *eth_read(struct index_node *inode, uint8_t *buffer, uint32_t length) {
	uint8_t	 *buf	 = kmalloc(ETH_MAX_FRAME_SIZE);
	device_t *device = list_owner(&inode->device->listm, device_t, listm);
	device->drv_obj->function.driver_read(device, buf, 0, 0);
}

void eth_write(device_t *device, uint8_t *src_mac, uint8_t *dst_mac, uint16_t type, uint8_t *data,
			   uint8_t length) {
	eth_frame_t *frame;
	uint16_t	 size = MAX(ETH_MIN_FRAME_SIZE, length + 16);
	uint8_t		*buf  = kmalloc(size);
	frame			  = (eth_frame_t *)buf;

	memcpy(frame->src_mac, src_mac, 6);
	memcpy(frame->dest_mac, dst_mac, 6);
	frame->type = HOST2BE_WORD(type);
	memcpy(buf + 14, data, length);
	memset(buf + length + 14, 0, size - length - 14);
	device->drv_obj->function.driver_write(device, buf, 0, size);
	kfree(buf);
}

void eth_service(void *arg) {
	device_t	*device	  = (device_t *)arg;
	list_t		*listhead = (list_t *)device->dm_private;
	uint8_t		*buf	  = kmalloc(ETH_MAX_FRAME_SIZE);
	uint16_t	 offset	  = 0;
	eth_frame_t *frame	  = kmalloc(sizeof(eth_frame_t));
	device->drv_obj->function.driver_open(device);
	while (1) {
		device->drv_obj->function.driver_read(device, buf + offset, 0, ETH_MAX_FRAME_SIZE - offset);
		memcpy(frame, buf, sizeof(eth_frame_t) - sizeof(uint8_t) - sizeof(uint16_t) - sizeof(list_t));
		frame->type = BE2HOST_WORD((uint16_t)(buf + sizeof(uint8_t) * 12));
	}
}

void eth_dm_start() {
	int		  i = 0;
	eth_t	 *eth;
	char	  thread_name[] = "ethn service";
	device_t *cur, *next;

	eth		   = eth_dm.private_data;
	eth->lists = kmalloc(sizeof(list_t) * eth->count);

	list_for_each_owner_safe (cur, next, &eth_dm.dev_listhead, listm) {
		cur->dm_private = (void *)&eth->lists[i];
		thread_start(thread_name, THREAD_DEFAULT_PRIO, eth_service, cur);
		i++;
	}
}

void eth_device_register(device_t *device, char *name) {
	eth_t *eth;

	eth = eth_dm.private_data;
	eth->count++;

	if (default_net_device == NULL) { default_net_device = device; }

	string_init(&device->name);
	string_new(&device->name, name, strlen(name));
	list_add_tail(&device->listm, &eth_dm.dev_listhead);
}

void eth_device_unregister(device_t *device) {
	eth_t *eth;

	eth = eth_dm.private_data;
	string_del(&device->name);
	list_del(&device->listm);
}
