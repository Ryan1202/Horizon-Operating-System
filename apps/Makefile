sinclude ../scripts/env.mk
sinclude ../scripts/localenv.mk

LIBS_DIR	:= ../libs

X_ASFLAGS	:= $(ENV_AFLAGS)
X_CFLAGS	:= $(ENV_CFLAGS)

X_INCDIRS	:= ../include/

X_LDFLAGS	:= $(ENV_LDFLAGS) $(ENV_APP_LD_SCRIPT)
X_LIBDIRS	:= $(LIBS_DIR)

X_LIBS		:=	libsys.a

AS			:=	$(ENV_AS)
LD			:=  $(ENV_LD)

export AS LD

MODULE += test