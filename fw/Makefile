# ---- Project ----
TARGET := app/main
OUTPUT := output
NAME := MagneticLevitation

ADDITIONAL_C_FILES :=

# ---- Chip ----
TARGET_MCU         := CH32X035
TARGET_MCU_PACKAGE := CH32X035G8U6

# ---- Paths ----
CH32FUN := lib

EXTRA_CFLAGS += -Iinc

ADDITIONAL_C_FILES := $(filter-out app/main.c,$(wildcard app/*.c))
include $(CH32FUN)/ch32fun.mk

# ---- Generated files ----
BUILD_ARTIFACTS := \
	app/main.elf \
	app/main.bin \
	app/main.hex \
	app/main.map

# ---- USB bootloader flashing via wchisp ----
WCHISP ?= wchisp

.PHONY: all flash clean package tidy

all: build

flash: $(TARGET).bin
	@trap '$(MAKE) tidy' EXIT; \
	$(WCHISP) flash $(TARGET).bin

package: build
	mkdir -p $(OUTPUT)
	cp app/main.elf $(OUTPUT)/$(NAME).elf
	cp app/main.bin $(OUTPUT)/$(NAME).bin
	cp app/main.hex $(OUTPUT)/$(NAME).hex
	cp app/main.map $(OUTPUT)/$(NAME).map
	$(MAKE) tidy

tidy:
	rm -f $(BUILD_ARTIFACTS)

clean:
	$(MAKE) cv_clean
	$(MAKE) tidy