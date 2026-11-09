ifeq ($(OS),Windows_NT)
    UNAME_S := Windows_NT
else
    UNAME_S := $(shell uname -s)
endif

MAKE	=	make
MKDIR	=	mkdir
RM		=	rm
CP		=	cp

ifeq ($(UNAME_S),Darwin)
# macOS 下 cp 会直接覆盖二进制，导致系统的签名缓存不会更新，所以使用 ditto
	CP	=	ditto
endif

OUTPUT_DIR = bin/

IMAGETOOL_DIR = imagetool
CONFIGURATOR_DIR = configurator

.PHONY: all imgtool config clean

source_files = $(wildcard $(1)*) $(foreach dir,$(filter %/,$(wildcard $(1)*/)),$(call source_files,$(dir)))

TOOLCHAIN_FILES = $(wildcard ../rust-toolchain ../rust-toolchain.toml ../.cargo/config ../.cargo/config.toml)
IMAGETOOL_INPUTS = $(call source_files,$(IMAGETOOL_DIR)/src/) $(IMAGETOOL_DIR)/src $(IMAGETOOL_DIR)/Cargo.toml $(wildcard $(IMAGETOOL_DIR)/Cargo.lock) $(TOOLCHAIN_FILES)
CONFIGURATOR_INPUTS = $(call source_files,$(CONFIGURATOR_DIR)/src/) $(CONFIGURATOR_DIR)/src $(CONFIGURATOR_DIR)/Cargo.toml $(wildcard $(CONFIGURATOR_DIR)/Cargo.lock) $(TOOLCHAIN_FILES)

all: imgtool config

imgtool: $(OUTPUT_DIR)imagetool

config: $(OUTPUT_DIR)configurator

$(OUTPUT_DIR)imagetool: $(IMAGETOOL_INPUTS) $(wildcard $(IMAGETOOL_DIR)/target/release/imagetool) | bin
	cargo build --release --manifest-path $(IMAGETOOL_DIR)/Cargo.toml
	$(CP)	$(IMAGETOOL_DIR)/target/release/imagetool $(OUTPUT_DIR)imagetool

$(OUTPUT_DIR)configurator: $(CONFIGURATOR_INPUTS) $(wildcard $(CONFIGURATOR_DIR)/target/release/configurator) | bin
	cargo build --release --manifest-path $(CONFIGURATOR_DIR)/Cargo.toml
	$(CP)	$(CONFIGURATOR_DIR)/target/release/configurator $(OUTPUT_DIR)configurator

bin:
	$(MKDIR) $(OUTPUT_DIR)

clean:
	cargo clean --manifest-path $(IMAGETOOL_DIR)/Cargo.toml
	cargo clean --manifest-path $(CONFIGURATOR_DIR)/Cargo.toml
	$(RM) -rf $(OUTPUT_DIR)
