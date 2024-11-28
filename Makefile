DD			=	dd
CP			=	cp
GRUB_MKIMG	=	grub-mkimage
MKDIR		=	mkdir
PYTHON		=	python

ARCH		:= x86

export ARCH

ifeq ($(ARCH), x86)
	QEMU		=	qemu-system-i386
	TARGET_PLATFORM	=	i386-pc
endif

FD_IMG		=	./horizon.img
HD_IMG		=	./hd0.img
HD_SIZE		=	64M
RESV_SIZE	=	1M

APP_SRC		=	apps
KERNEL_SRC	=	src
TOOL_SRC	=	tools
LIB_SRC		=	libs
ARCH_DIR	=	$(KERNEL_SRC)/arch/x86
BOOT_DIR	=	$(ARCH_DIR)/boot
DRIVER_DIR	=	$(ARCH_DIR)/drivers
KERNEL_DIR	=	$(KERNEL_SRC)/kernel
FS_DIR		=	$(KERNEL_DIR)/fs
LIB_DIR		=	$(KERNEL_SRC)/lib
DISK_DIR	=	disk

LOADER_OFF	=	2
LOADER_CNTS	=	8

KERNEL_OFF	=	10
KERNEL_CNTS	=	600

BOOT_BIN	=	$(BOOT_DIR)/boot.bin
LOADER_BIN	=	$(BOOT_DIR)/loader.bin
KERNEL_ELF	=	$(KERNEL_SRC)/kernel.elf

IMAGETOOL	=	$(TOOL_SRC)/bin/imagetool

.PHONY: cld

cld: kernel libs

run: kernel libs qemu

run_dbg: kernel libs qemu_dbg

hd: tool
	$(PYTHON) ./install_grub.py \
		--image $(HD_IMG) \
		--fs fat32 \
		--platform $(TARGET_PLATFORM)

app:
	$(MAKE) -s -C $(APP_SRC)

kernel:
	$(MAKE) -s -C $(BOOT_DIR)
	$(MAKE) -s -C $(KERNEL_SRC)
	$(IMAGETOOL) $(HD_IMG) copy $(KERNEL_ELF) /p0/kernel.elf
	
tool:
	$(MAKE) -s -C $(TOOL_SRC)

lib:
	$(MAKE) -s -C $(LIB_SRC)

clean:
	$(MAKE) -s -C $(APP_SRC) clean
	$(MAKE) -s -C $(BOOT_DIR) clean
	$(MAKE) -s -C $(KERNEL_SRC) clean
	$(MAKE) -s -C $(LIB_SRC) clean

clean_all:
	$(MAKE) -s -C $(APP_SRC) clean
	$(MAKE) -s -C $(BOOT_DIR) clean
	$(MAKE) -s -C $(KERNEL_SRC) clean
	$(MAKE) -s -C $(LIB_SRC) clean
	$(MAKE) -s -C $(TOOL_SRC) clean

writehd: $(HD_IMG)
	$(IMAGETOOL) $(HD_IMG) copy -r $(DISK_DIR) /p0/

# qemu 7.1后取消了-soundhw，改用-audio
qemu_dbg:
	$(QEMU) \
	-s -S \
	-monitor stdio \
	-m 1024 \
	-hda $(HD_IMG) \
	-usb \
	-device usb-kbd \
	-device usb-mouse \
	-audio pa,model=sb16 \
	-device rtl8139,netdev=nc1 \
	-netdev user,id=nc1,hostfwd=tcp::5555-:80 \
	-object filter-dump,id=f1,netdev=nc1,file=dump.dat \
	-boot c
	
qemu:
	$(QEMU) \
	-monitor stdio \
	-m 1024 \
	-hda $(HD_IMG) \
	-usb \
	-device usb-mouse \
	-audio pa,model=sb16 \
	-device rtl8139,netdev=nc1 \
	-netdev user,id=nc1,hostfwd=tcp::5555-:80 \
	-object filter-dump,id=f1,netdev=nc1,file=dump.dat \
	-boot c