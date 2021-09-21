DD		=	dd

QEMU	=	qemu-system-i386

FD_IMG		=	./horizon.img
HD_IMG		=	./hd0.img

KERNEL_SRC	=	src
LIB_SRC		=	lib
ARCH		=	$(KERNEL_SRC)/arch/x86
BOOT_DIR	=	$(ARCH)/boot
DRIVER_DIR	=	$(ARCH)/drivers
KERNEL_DIR	=	$(KERNEL_SRC)/kernel
FS_DIR		=	$(KERNEL_DIR)/fs
LIB_DIR		=	$(KERNEL_SRC)/lib

LOADER_OFF	=	2
LOADER_CNTS	=	8

KERNEL_OFF	=	10
KERNEL_CNTS	=	600

BOOT_BIN	=	$(BOOT_DIR)/boot.bin
LOADER_BIN	=	$(BOOT_DIR)/loader.bin
KERNEL_ELF	=	$(KERNEL_SRC)/kernel.elf

.PHONY: cld

cld: kernel libs write

run: kernel libs write qemu

run_dbg: kernel libs write qemu_dbg

kernel:
	$(MAKE) -s -C $(BOOT_DIR)
	$(MAKE) -s -C $(KERNEL_SRC)

libs:
	$(MAKE) -s -C $(LIB_SRC)

clean:
	$(MAKE) -s -C $(BOOT_DIR) clean
	$(MAKE) -s -C $(KERNEL_SRC) clean
	$(MAKE) -s -C $(LIB_SRC) clean

write:
#先覆盖掉软盘内的内容再写入，在没有软盘映像时能创建映像
	$(DD) if=/dev/zero of=$(FD_IMG) bs=512 count=2880 conv=notrunc
	$(DD) if=$(BOOT_BIN) of=$(FD_IMG) bs=512 count=1 conv=notrunc
	$(DD) if=$(LOADER_BIN) of=$(FD_IMG) bs=512 seek=$(LOADER_OFF) count=$(LOADER_CNTS) conv=notrunc
	$(DD) if=$(KERNEL_ELF) of=$(FD_IMG) bs=512 seek=$(KERNEL_OFF) count=$(KERNEL_CNTS) conv=notrunc

qemu_dbg:
	$(QEMU) \
	-s -S \
	-monitor stdio \
	-m 1024 \
	-drive file=$(FD_IMG),index=0,if=floppy,format=raw \
	-hda $(HD_IMG) \
	-boot a
	
qemu:
	$(QEMU) \
	-monitor stdio \
	-m 1024 \
	-drive file=$(FD_IMG),index=0,if=floppy,format=raw \
	-hda $(HD_IMG) \
	-boot a