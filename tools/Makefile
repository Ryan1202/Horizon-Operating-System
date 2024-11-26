MAKE	=	make
MKDIR	=	mkdir
RM		=	rm

OUTPUT_DIR = bin/

IMAGETOOL_DIR = imagetool/

.PHONY: all

all: $(OUTPUT_DIR) imgtool

$(OUTPUT_DIR):
	$(MKDIR) $(OUTPUT_DIR)

imgtool:
	cargo build --release --manifest-path $(IMAGETOOL_DIR)/Cargo.toml
	cp	$(IMAGETOOL_DIR)target/release/imagetool $(OUTPUT_DIR)

clean:
	cargo clean --manifest-path imagetool/Cargo.toml
	$(RM) -rf $(OUTPUT_DIR)