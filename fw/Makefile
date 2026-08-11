# ---- Project ----
TARGET := src/main
OUTPUT := output
NAME := MagneticLevitation

ADDITIONAL_C_FILES :=

# ---- Chip ----
TARGET_MCU         := CH32X035
TARGET_MCU_PACKAGE := CH32X035G8U6

# ---- Paths ----
CH32FUN := ch32fun

EXTRA_CFLAGS += -Iinc -Isrc/control -Isrc/usb -Isrc/comm -Isrc/periph -Isrc/core -Isrc/system

ADDITIONAL_C_FILES := $(filter-out src/main.c,$(shell find src -type f -name '*.c'))
include $(CH32FUN)/ch32fun.mk

# ---- Generated files ----
BUILD_ARTIFACTS := \
	src/main.elf \
	src/main.bin \
	src/main.hex \
	src/main.map

# ---- USB bootloader flashing via wchisp ----
WCHISP ?= wchisp

.PHONY: all flash clean package tidy

all: build

flash: $(TARGET).bin
	@trap '$(MAKE) tidy' EXIT; \
	$(WCHISP) flash $(TARGET).bin

package: build
	mkdir -p $(OUTPUT)
	cp src/main.elf $(OUTPUT)/$(NAME).elf
	cp src/main.bin $(OUTPUT)/$(NAME).bin
	cp src/main.hex $(OUTPUT)/$(NAME).hex
	cp src/main.map $(OUTPUT)/$(NAME).map
	$(MAKE) tidy

tidy:
	rm -f $(BUILD_ARTIFACTS)

clean:
	$(MAKE) cv_clean
	$(MAKE) tidy