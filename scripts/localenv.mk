# env var

ENV_CFLAGS	:= -march=i486 -fno-builtin -Wall -Wunused -m32 -std=gnu99 -fno-stack-protector
# kernel name & version
ENV_CFLAGS	+= -DKERNEL_NAME=\"horizon\" -DKERNEL_VERSION=\"0.0.1\"

ENV_AFLAGS	:= -f elf
ENV_LDFLAGS	:= -no-pie

# MacOS special
ifeq ($(shell uname),Darwin)
	ENV_LD		:=  i386-elf-ld -m elf_i386
else
	ENV_LD		:=  ld -m elf_i386
endif

ENV_AS		:= nasm