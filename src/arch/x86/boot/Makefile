NASM	=	nasm
RM		=	rm

BOOT_BIN = boot.bin
LOAD_BIN = loader.bin

.PHONY: all

all: clean compile

compile: $(BOOT_BIN) $(LOAD_BIN)

clean:
	-$(RM) *.bin

$(BOOT_BIN): boot.asm
	$(NASM) -o $@ $<

$(LOAD_BIN): loader.asm
	$(NASM) -o $@ $<
