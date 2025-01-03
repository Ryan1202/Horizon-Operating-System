#ifndef _DRIVER_INTERFACE_H
#define _DRIVER_INTERFACE_H

#ifdef ARCH_X86

#include "kernel/func.h"

#define io_in_byte(port)		 io_in8(port)
#define io_in_word(port)		 io_in16(port)
#define io_in_dword(port)		 io_in32(port)
#define io_out_byte(port, data)	 io_out8(port, data)
#define io_out_word(port, data)	 io_out16(port, data)
#define io_out_dword(port, data) io_out32(port, data)

#define io_stream_in_byte(port, buffer, nr)	  io_stream_in8(port, buffer, nr)
#define io_stream_in_word(port, buffer, nr)	  io_stream_in16(port, buffer, nr)
#define io_stream_in_dword(port, buffer, nr)  io_stream_in32(port, buffer, nr)
#define io_stream_out_byte(port, buffer, nr)  io_stream_out8(port, buffer, nr)
#define io_stream_out_word(port, buffer, nr)  io_stream_out16(port, buffer, nr)
#define io_stream_out_dword(port, buffer, nr) io_stream_out32(port, buffer, nr)

#define read_msr(msr, l, h)	 cpu_RDMSR(msr, l, h)
#define write_msr(msr, l, h) cpu_WRMSR(msr, l, h)

#define enable_interrupt()	io_sti()
#define disable_interrupt() io_cli()

#define load_interrupt_status()		   io_load_eflags();
#define store_interrupt_status(status) io_store_eflags(status);

#else
#error Driver: Unsupport Architecture
#endif

#include <kernel/console.h>
#define print_error_with_position(str, ...) \
	printk(COLOR_RED __FILE__ " Line %d: " str, __LINE__, ##__VA_ARGS__)
#define print_error(source, str, ...) \
	printk(COLOR_RED "[%s]" str, source, ##__VA_ARGS__)
#define print_device_info(device, str, ...) \
	printk("[%s]" str, device->name.text, ##__VA_ARGS__)
#define print_driver_info(driver, str, ...) \
	printk("[%s]" str, driver.name.text, ##__VA_ARGS__)

#include "kernel/list.h"
struct Device;
typedef void (*DeviceIrqHandler)(struct Device *device);
typedef struct DeviceIrq {
	list_t			 list;
	int				 irq;
	struct Device	*device;
	DeviceIrqHandler handler;
} DeviceIrq;

#include "kernel/driver.h"
#include "stdint.h"
typedef struct DriverRemappedMemory {
	list_t	 list;
	uint32_t vir_start;
	uint32_t phy_start;
	uint32_t size;
} DriverRemappedMemory;

enum DriverResult register_device_irq(DeviceIrq *dev_irq);
enum DriverResult unregister_device_irq(DeviceIrq *dev_irq);
void			  device_irq_handler(int irq);
DriverResult	  driver_remap_memory(
		 Driver *in_driver, uint32_t in_physical_address, uint32_t in_size,
		 uint32_t *out_virtual_address);

#endif