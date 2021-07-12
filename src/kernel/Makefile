SRC		+=	console.c
SRC		+=	fifo.c
SRC		+=	font-default.c
SRC		+=	main.c
SRC		+=	memory.c
SRC		+=	sync.c
SRC		+=	thread.c
SRC		+=	initcall.c
SRC		+=	driver.c

#INCLUDE_PATH = ../include/ ../$(ARCH)/include/

#ASM_FLAGS	=	-I $(INCLUDE_PATH) -f elf
#C_FLAGS		=	-I $(INCLUDE_PATH) -c -g -fno-stack-protector -fno-builtin -Wall -Wunused -m32

#OBJS	= 	main.o \
			console.o \
			memory.o \
			fifo.o \
			font-default.o \
			thread.o \
			sync.o

#.PHONY: clean

#compile: $(OBJS)

#clean:
#	rm *.o
#	rm ../kernel.elf

#%.o: %.asm
#	$(NASM) $(ASM_FLAGS) -o $@ $<
	
#%.o: %.c
#	$(CC) $(C_FLAGS) -o $@ $<