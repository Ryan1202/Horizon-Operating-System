#ifndef _DRIVER_INTERFACE_H
#define _DRIVER_INTERFACE_H

#include <kernel/list.h>
#include <multiple_return.h>
#include <stdint.h>

#ifdef ARCH_X86

#include <kernel/func.h>

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

#define read_msr(msr, l, h)	 cpu_rdmsr(msr, l, h)
#define write_msr(msr, l, h) cpu_wrmsr(msr, l, h)

#define enable_interrupt()	io_sti()
#define disable_interrupt() io_cli()

#define load_interrupt_status()		   io_load_eflags();
#define store_interrupt_status(status) io_store_eflags(status);

#define save_and_disable_interrupt() save_eflags_cli()

#else
#error Driver: Unsupport Architecture
#endif

#include <kernel/console.h>
#define print_error_with_position(str, ...)                        \
	printk(                                                        \
		COLOR_RED __FILE__ " Line %d: " str COLOR_RESET, __LINE__, \
		##__VA_ARGS__)
#define print_error(source, str, ...) \
	printk(COLOR_RED "[" source "]" str COLOR_RESET, ##__VA_ARGS__)
#define print_warning(source, str, ...) \
	printk(COLOR_BYELLOW "[%s]" str COLOR_RESET, source, ##__VA_ARGS__)
#define print_info(source, str, ...) \
	printk(COLOR_RESET "[%s]" str, source, ##__VA_ARGS__)

struct PhysicalDevice;
typedef void (*DeviceIrqHandler)(void *device);

typedef enum {
	IRQ_MODE_SHARED,
	IRQ_MODE_EXCLUSIVE,
} IrqMode;

typedef struct DeviceIrq {
	list_t	list;
	list_t	irq_list;
	int		irq;
	IrqMode mode;

	void				  *arg;
	DeviceIrqHandler	   handler;
	struct PhysicalDevice *physical_device;
} DeviceIrq;

typedef struct DriverRemappedMemory {
	list_t	 list;
	uint32_t vir_start;
	uint32_t phy_start;
	uint32_t size;
} DriverRemappedMemory;

struct Driver;
enum DriverResult register_device_irq(
	DEF_MRET(DeviceIrq *, device_irq), struct PhysicalDevice *physical_device,
	void *arg, int irq, DeviceIrqHandler irq_handler, IrqMode mode);
enum DriverResult unregister_device_irq(DeviceIrq *dev_irq);
enum DriverResult enable_device_irq(DeviceIrq *dev_irq);
enum DriverResult disable_device_irq(DeviceIrq *dev_irq);

void			  device_irq_handler(int irq);
enum DriverResult driver_remap_memory(
	struct Driver *in_driver, uint32_t in_physical_address, uint32_t in_size,
	uint32_t *out_virtual_address);

#define kmalloc_from_template(template)            \
	({                                             \
		void *data = kmalloc(sizeof(template));    \
		memcpy(data, &template, sizeof(template)); \
		data;                                      \
	})

#endif