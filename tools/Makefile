MAKE	=	make
MKDIR	=	mkdir
RM		=	rm

OUTPUT_DIR = bin/

IMAGETOOL_DIR = imagetool
CONFIGURATOR_DIR = configurator

.PHONY: all

all: bin imgtool config

imgtool:
	cargo build --release --manifest-path $(IMAGETOOL_DIR)/Cargo.toml
	cp	$(IMAGETOOL_DIR)/target/release/imagetool $(OUTPUT_DIR)

config:
	cargo build --release --manifest-path $(CONFIGURATOR_DIR)/Cargo.toml
	cp	$(CONFIGURATOR_DIR)/target/release/configurator $(OUTPUT_DIR)

bin:
	$(MKDIR) $(OUTPUT_DIR)

clean:
	cargo clean --manifest-path $(IMAGETOOL_DIR)/Cargo.toml
	cargo clean --manifest-path $(CONFIGURATOR_DIR)/Cargo.toml
	$(RM) -rf $(OUTPUT_DIR)