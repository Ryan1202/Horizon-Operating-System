sinclude ../scripts/env.mk
sinclude ../scripts/localenv.mk

ARCH	:=	arch/x86

X_ASFLAGS	:= $(ENV_AFLAGS) -I $(ARCH)/include/ -g
X_CFLAGS    := $(ENV_CFLAGS) -fno-strict-aliasing -g

X_INCDIRS	:= ../include/ ./include/ $(ARCH)/include/

X_LDFLAGS	:=  $(ENV_LDFLAGS)

AS			:=	$(ENV_AS)
LD			:=  $(ENV_LD)

export AS LD
export ARCH

SRC			:=	$(ARCH)/ fs/ kernel/ lib/

NAME		:=	kernel.elf

define CUSTOM_TARGET_CMD
echo [KERNEL] $@; \
$(LD) $(X_LDFLAGS) -T $(ARCH)/kernel.lds -o $@ $(X_OBJS)
endef