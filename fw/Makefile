# ---- Project ----
TARGET := src/main
OUTPUT := output
NAME := MagneticLevitation

ADDITIONAL_C_FILES :=

# ---- Chip ----
# Family select (feeds ch32fun.mk's chip-detection block)
TARGET_MCU         := CH32X035
# Exact part, used to pick the flash/RAM split (G8 -> 62K flash / 20K SRAM)
TARGET_MCU_PACKAGE := CH32X035G8U6

# ---- Paths ----
CH32FUN := ch32fun

# Extra include dir for our own headers (inc/main.h)
EXTRA_CFLAGS += -Iinc

include $(CH32FUN)/ch32fun.mk

# ---- USB bootloader (DFU) flashing via wchisp ----
# CH32X035 has a native USB bootloader: hold BOOT/short BOOT0, plug in via USB,
# then flash with wchisp instead of minichlink/WCH-Link.
WCHISP ?= wchisp

.PHONY: flash
flash: $(TARGET).bin
	$(WCHISP) flash $(TARGET).bin

.PHONY: clean
clean: cv_clean

.PHONY: all
all: build

.PHONY: package
package: build
	mkdir -p $(OUTPUT)
	cp src/main.elf $(OUTPUT)/$(NAME).elf
	cp src/main.bin $(OUTPUT)/$(NAME).bin
	cp src/main.hex $(OUTPUT)/$(NAME).hex
	cp src/main.map $(OUTPUT)/$(NAME).map

	rm src/main.elf
	rm src/main.bin
	rm src/main.hex
	rm src/main.map