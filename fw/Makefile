# ---- Project ----
# Resolve the directory this Makefile lives in so the Makefile can be
# invoked from the workspace root or from inside `fw/`.
MAKEFILE_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

TARGET := $(MAKEFILE_DIR)/app/main
OUTPUT := $(MAKEFILE_DIR)/output
NAME := MagneticLevitation

ADDITIONAL_C_FILES :=

# ---- Chip ----
TARGET_MCU         := CH32X035
TARGET_MCU_PACKAGE := CH32X035G8U6

# ---- Paths ----
CH32FUN := $(MAKEFILE_DIR)/lib

# Include common include dirs (app, lib, optional inc)
EXTRA_CFLAGS += -I$(MAKEFILE_DIR)/inc -I$(MAKEFILE_DIR)/app -I$(MAKEFILE_DIR)/lib

# Collect additional C files from app/ (exclude main.c)
APP_C_FILES := $(filter-out $(MAKEFILE_DIR)/app/main.c,$(wildcard $(MAKEFILE_DIR)/app/*.c))
# Collect additional C files from lib/ (exclude ch32fun.c which is built as SYSTEM_C)
LIB_C_FILES := $(filter-out $(CH32FUN)/ch32fun.c,$(wildcard $(CH32FUN)/*.c))
# Combine
ADDITIONAL_C_FILES := $(APP_C_FILES) $(LIB_C_FILES)

include $(CH32FUN)/ch32fun.mk

# ---- Generated files ----
BUILD_ARTIFACTS := \
	$(TARGET).elf \
	$(TARGET).bin \
	$(TARGET).hex \
	$(TARGET).map

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