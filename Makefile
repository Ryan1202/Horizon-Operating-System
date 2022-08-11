DD		=	dd
CP		=	cp

QEMU	=	qemu-system-i386

FD_IMG		=	./horizon.img
HD_IMG		=	./hd0.img
HD_SIZE		=	64

APP_SRC		=	apps
KERNEL_SRC	=	src
LIB_SRC		=	libs
ARCH		=	$(KERNEL_SRC)/arch/x86
BOOT_DIR	=	$(ARCH)/boot
DRIVER_DIR	=	$(ARCH)/drivers
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

.PHONY: cld

cld: kernel libs writefd

run: kernel libs writefd qemu

run_dbg: kernel libs writefd qemu_dbg

hd:
	$(DD) if=/dev/zero of=$(HD_IMG) bs=1M count=$(HD_SIZE)
	@parted $(HD_IMG) -s mklabel msdos mkpart primary fat32 0% 100%
	@kpartx -av hd0.img
	@umount /dev/mapper/loop0p1
	@mkfs.vfat -F 32 /dev/mapper/loop0p1
	@kpartx -d hd0.img
	@chmod -R 777 $(HD_IMG)

app:
	$(MAKE) -s -C $(APP_SRC)

kernel:
	$(MAKE) -s -C $(BOOT_DIR)
	$(MAKE) -s -C $(KERNEL_SRC)

lib:
	$(MAKE) -s -C $(LIB_SRC)

clean:
	$(MAKE) -s -C $(APP_SRC) clean
	$(MAKE) -s -C $(BOOT_DIR) clean
	$(MAKE) -s -C $(KERNEL_SRC) clean
	$(MAKE) -s -C $(LIB_SRC) clean

writefd:
#先覆盖掉软盘内的内容再写入，在没有软盘映像时能创建映像
	$(DD) if=/dev/zero of=$(FD_IMG) bs=512 count=2880 conv=notrunc
	$(DD) if=$(BOOT_BIN) of=$(FD_IMG) bs=512 count=1 conv=notrunc
	$(DD) if=$(LOADER_BIN) of=$(FD_IMG) bs=512 seek=$(LOADER_OFF) count=$(LOADER_CNTS) conv=notrunc
	$(DD) if=$(KERNEL_ELF) of=$(FD_IMG) bs=512 seek=$(KERNEL_OFF) count=$(KERNEL_CNTS) conv=notrunc

writehd:
	@kpartx -av $(HD_IMG)
	@mount /dev/mapper/loop0p1 /mnt
	$(CP) -r $(DISK_DIR)/* /mnt
	@umount /mnt
	@kpartx -d $(HD_IMG)

# qemu 7.1后取消了-soundhw，改用-audio
qemu_dbg:
	$(QEMU) \
	-s -S \
	-monitor stdio \
	-m 1024 \
	-drive file=$(FD_IMG),index=0,if=floppy,format=raw \
	-hda $(HD_IMG) \
	-audio pa,model=sb16 \
	-boot a
	
qemu:
	$(QEMU) \
	-monitor stdio \
	-m 1024 \
	-drive file=$(FD_IMG),index=0,if=floppy,format=raw \
	-hda $(HD_IMG) \
	-audio pa,model=sb16 \
	-boot a