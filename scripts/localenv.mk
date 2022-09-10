# env var

ENV_CFLAGS	:= -march=i486 -fno-builtin -Wall -Wunused -m32 -std=gnu99 -fno-stack-protector -nostdinc -nostdlib
# kernel name & version
ENV_CFLAGS	+= -DKERNEL_NAME=\"horizon\" -DKERNEL_VERSION=\"0.0.1\"

ENV_AFLAGS	:= -g -f elf
ENV_LDFLAGS	:= -g -no-pie

ENV_APP_LD_SCRIPT	:= -T ../apps/app.lds

# MacOS special
ifeq ($(shell uname),Darwin)
	ENV_LD		:=  i386-elf-ld -m elf_i386
else
	ENV_LD		:=  ld -m elf_i386
endif

ENV_AS		:= nasm