#ifndef MULTIBOOT2_H

#include <stdint.h>

// Multiboot2 Info Tags
#define MBIT_END				  0
#define MBIT_BASE_MEMINFO		  4
#define MBIT_BOOT_DEVICE		  5
#define MBIT_CMDLINE			  1
#define MBIT_MODULES			  3
#define MBIT_ELF_SYMBOLS		  9
#define MBIT_MEM_MAP			  6
#define MBIT_BOOTOADER_NAME		  2
#define MBIT_APM_TABLE			  10
#define MBIT_VBE_INFO			  7
#define MBIT_FRAMEBUFER_INFO	  8
#define MBIT_ELF32_SYMTAB		  11
#define MBIT_ELF64_SYMTAB		  12
#define MBIT_SMBIOS_TABLE		  13
#define MBIT_ACPI_OLD_RSDP		  14
#define MBIT_ACPI_NEW_RSDP		  15
#define MBIT_NETWORK_INFO		  16
#define MBIT_EFI32_MEM_MAP		  17
#define MBIT_EFI_BOOTDEV_NOT_TERM 18
#define MBIT_EFI32_IMAGE_HANDLE	  19
#define MBIT_EFI64_IMAGE_HANDLE	  20
#define MBIT_LOAD_BASE_ADDR		  21

struct framebuffer_tag {
	uint32_t type;
	uint32_t size;
	uint32_t framebuffer_addr[2];
	uint32_t framebuffer_pitch;
	uint32_t framebuffer_width;
	uint32_t framebuffer_height;
	uint8_t	 framebuffer_bpp;
	uint8_t	 framebuffer_type;
	uint16_t reserved;
};

struct vbe_info_tag {
	uint32_t type;
	uint32_t size;
	uint16_t vbe_mode;
	uint16_t vbe_interface_seg;
	uint16_t vbe_interface_off;
	uint16_t vbe_interface_len;
	uint8_t	 vbe_control_info[512];
	uint8_t	 vbe_mode_info[256];
};

struct base_meminfo_tag {
	uint32_t type;
	uint32_t size;
	uint32_t mem_lower;
	uint32_t mem_upper;
};

struct mem_map_tag {
	uint32_t type;
	uint32_t size;
	uint32_t entry_size;
	uint32_t entry_version;
	struct mmap_entries {
		uint32_t entry_size;
		uint32_t entry_version;
	} entries[];
};

#endif
