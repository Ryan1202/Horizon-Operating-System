sinclude ../scripts/env.mk
sinclude ../scripts/localenv.mk

ifeq ($(ARCH), x86)
ARCH_DIR	:=	./arch/x86
endif

X_ASFLAGS	:= $(ENV_AFLAGS) -I $(ARCH_DIR)/include/ -g
X_CFLAGS    := $(ENV_CFLAGS) -fno-strict-aliasing -g
ifeq ($(ARCH), x86)
X_CFLAGS	+= -DARCH_X86
endif

X_INCDIRS	:= ../include/ ./include/ $(ARCH_DIR)/include/

X_LDFLAGS	:=  $(ENV_LDFLAGS)

AS			:=	$(ENV_AS)
LD			:=  $(ENV_LD)

export AS LD
export ARCH_DIR

SRC			:=	$(ARCH_DIR)/ fs/ driver/ kernel/ lib/ network/

NAME		:=	kernel.elf

define CUSTOM_TARGET_CMD
echo [KERNEL] $@; \
$(LD) $(X_LDFLAGS) -T $(ARCH_DIR)/kernel.lds -o $@ $(X_OBJS)
endef