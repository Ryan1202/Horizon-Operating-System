#include <driver/interrupt_dm.h>
#include <kernel/device.h>
#include <kernel/driver.h>
#include <kernel/driver_interface.h>
#include <kernel/list.h>
#include <kernel/memory.h>
#include <kernel/page.h>
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

DriverResult register_device_irq(DeviceIrq *dev_irq) {
	if (dev_irq->irq > 16) {
		print_error_with_position("invalid irq number:%d\n", dev_irq->irq);
		return DRIVER_RESULT_INVALID_IRQ_NUMBER;
	}
	dev_irq->irq = interrupt_redirect_irq(dev_irq->irq);
	list_add_tail(&dev_irq->list, &device_irq_lists[dev_irq->irq]);
	return DRIVER_RESULT_OK;
}

DriverResult unregister_device_irq(DeviceIrq *dev_irq) {
	list_del(&dev_irq->list);
	return DRIVER_RESULT_OK;
}

void device_irq_handler(int irq) {
	DeviceIrq *cur;
	if (list_empty(&device_irq_lists[irq])) return;
	list_for_each_owner (cur, &device_irq_lists[irq], list) {
		cur->handler(cur->device);
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
			return DRIVER_RESULT_OK;
		}
	}

	MemoryResult result =
		MEMORY_RESULT_PRINT_CALL(remap, start, end - start, &tmp);
	if (result != MEMORY_RESULT_OK) {
		printk(
			"Driver Interface: remap memory:0x%08x(size: %d) failed\n",
			in_physical_address, in_size);
		return DRIVER_RESULT_OTHER_ERROR;
	}
	if (virtual_address == 0) { virtual_address = tmp; }

	DriverRemappedMemory *remapped_memory =
		kmalloc(sizeof(DriverRemappedMemory));
	remapped_memory->size	   = end - start;
	remapped_memory->vir_start = virtual_address;
	remapped_memory->phy_start = start;
	list_add_tail(&remapped_memory->list, &in_driver->remapped_memory_lh);
	*out_virtual_address = virtual_address;
	return DRIVER_RESULT_OK;
}
