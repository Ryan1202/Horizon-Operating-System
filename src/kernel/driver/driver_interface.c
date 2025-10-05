#include <driver/interrupt/interrupt_dm.h>
#include <kernel/device.h>
#include <kernel/driver.h>
#include <kernel/driver_interface.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/page.h>
#include <multiple_return.h>
#include <objects/object.h>
#include <stdint.h>

list_t device_irq_lists[16] = {
	LIST_HEAD_INIT(device_irq_lists[0]),  LIST_HEAD_INIT(device_irq_lists[1]),
	LIST_HEAD_INIT(device_irq_lists[2]),  LIST_HEAD_INIT(device_irq_lists[3]),
	LIST_HEAD_INIT(device_irq_lists[4]),  LIST_HEAD_INIT(device_irq_lists[5]),
	LIST_HEAD_INIT(device_irq_lists[6]),  LIST_HEAD_INIT(device_irq_lists[7]),
	LIST_HEAD_INIT(device_irq_lists[8]),  LIST_HEAD_INIT(device_irq_lists[9]),
	LIST_HEAD_INIT(device_irq_lists[10]), LIST_HEAD_INIT(device_irq_lists[11]),
	LIST_HEAD_INIT(device_irq_lists[12]), LIST_HEAD_INIT(device_irq_lists[13]),
	LIST_HEAD_INIT(device_irq_lists[14]), LIST_HEAD_INIT(device_irq_lists[15]),
};

DriverResult register_device_irq(
	DEF_MRET(DeviceIrq *, device_irq), PhysicalDevice *physical_device,
	void *arg, int irq, DeviceIrqHandler irq_handler, IrqMode mode) {
	if (irq > 16) {
		print_error(
			"DeviceIrq", "invalid irq number:%d, device %s\n", irq,
			physical_device->object->name.text);
		return DRIVER_ERROR_INVALID_IRQ_NUMBER;
	}

	DeviceIrq *device_irq =
		list_first_owner_or_null(&device_irq_lists[irq], DeviceIrq, irq_list);
	if (device_irq != NULL && mode == IRQ_MODE_EXCLUSIVE) {
		print_error(
			"DeviceIrq",
			"Conflict irq number:%d, registered device %s, new device %s\n",
			irq, device_irq->physical_device->object->name.text,
			physical_device->object->name.text);
		return DRIVER_ERROR_CONFLICT;
	}

	device_irq = kmalloc(sizeof(DeviceIrq));
	if (device_irq == NULL) return DRIVER_ERROR_OUT_OF_MEMORY;
	device_irq->arg				= arg;
	device_irq->physical_device = physical_device;
	device_irq->handler			= irq_handler;
	device_irq->irq				= interrupt_redirect_irq(irq);

	MRET(device_irq) = device_irq;
	return DRIVER_OK;
}

DriverResult unregister_device_irq(DeviceIrq *dev_irq) {
	list_del(&dev_irq->list);
	list_del(&dev_irq->irq_list);
	int result = kfree(dev_irq);
	if (result < 0) return DRIVER_ERROR_MEMORY_FREE;
	return DRIVER_OK;
}

DriverResult enable_device_irq(DeviceIrq *dev_irq) {
	list_add_tail(&dev_irq->list, &device_irq_lists[dev_irq->irq]);
	return interrupt_enable_irq(dev_irq->irq);
}

DriverResult disable_device_irq(DeviceIrq *dev_irq) {
	list_del(&dev_irq->list);
	return interrupt_disable_irq(dev_irq->irq);
}

void device_irq_handler(int irq) {
	DeviceIrq *cur;
	if (list_empty(&device_irq_lists[irq])) return;
	list_for_each_owner (cur, &device_irq_lists[irq], list) {
		cur->handler(cur->arg);
	}
}

DriverResult driver_remap_memory(
	Driver *in_driver, uint32_t in_physical_address, uint32_t in_size,
	uint32_t *out_virtual_address) {
	// 地址对齐页大小
	uint32_t start = in_physical_address & 0xfffff000;
	uint32_t end   = (in_physical_address + in_size + 0xfff) & 0xfffff000;
	uint32_t tmp;
	uint32_t virtual_address = 0;

	// 调用前先检查是否已经被映射
	DriverRemappedMemory *cur;
	list_for_each_owner (cur, &in_driver->remapped_memory_lh, list) {
		if (cur->phy_start < start && cur->phy_start + cur->size > end) {
			virtual_address = cur->vir_start;
			return DRIVER_OK;
		}
	}

	MemoryResult result =
		MEMORY_RESULT_PRINT_CALL(remap, start, end - start, &tmp);
	if (result != MEMORY_RESULT_OK) {
		printk(
			"Driver Interface: remap memory:0x%08x(size: %d) failed\n",
			in_physical_address, in_size);
		return DRIVER_ERROR_OTHER;
	}
	if (virtual_address == 0) { virtual_address = tmp; }

	DriverRemappedMemory *remapped_memory =
		kmalloc(sizeof(DriverRemappedMemory));
	remapped_memory->size	   = end - start;
	remapped_memory->vir_start = virtual_address;
	remapped_memory->phy_start = start;
	list_add_tail(&remapped_memory->list, &in_driver->remapped_memory_lh);
	*out_virtual_address = virtual_address;
	return DRIVER_OK;
}
