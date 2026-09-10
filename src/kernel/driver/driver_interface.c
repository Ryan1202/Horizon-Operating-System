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

DriverResult driver_remap_memory(
	Driver *in_driver, size_t in_physical_address, uint32_t in_size,
	size_t *out_virtual_address) {
	// 地址对齐页大小
	size_t start = in_physical_address & 0xfffff000;
	size_t end	 = (in_physical_address + in_size + 0xfff) & 0xfffff000;
	size_t tmp;
	size_t virtual_address = 0;

	// 调用前先检查是否已经被映射
	DriverRemappedMemory *cur;
	list_for_each_owner (cur, &in_driver->remapped_memory_lh, list) {
		if (cur->phy_start < start && cur->phy_start + cur->size > end) {
			virtual_address = cur->vir_start;
			return DRIVER_OK;
		}
	}

	tmp = (size_t)ioremap(start, end - start, PAGE_CACHE_WRITE_COMBINE);

	if (virtual_address == 0) { virtual_address = tmp; }

	DriverRemappedMemory *remapped_memory =
		kzalloc(sizeof(DriverRemappedMemory));
	remapped_memory->size	   = end - start;
	remapped_memory->vir_start = virtual_address;
	remapped_memory->phy_start = start;
	list_add_tail(&remapped_memory->list, &in_driver->remapped_memory_lh);
	*out_virtual_address = virtual_address;
	return DRIVER_OK;
}
