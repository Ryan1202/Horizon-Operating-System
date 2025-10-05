#include <kernel/driver.h>
#include <kernel/initcall.h>
#include <kernel/list.h>

Driver x86_usb_hcd_driver;

static __init void usb_hcd_entry(void) {
	register_driver(&x86_usb_hcd_driver);
	return;
}

driver_initcall(usb_hcd_entry);