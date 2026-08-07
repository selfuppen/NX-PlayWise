ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set. Source devkitPro environment before building Device Lab)
endif

ifeq ($(strip $(DEVKITA64)),)
$(error DEVKITA64 is not set. Source devkitPro environment before building Device Lab)
endif

include ../../common/version.mk

TARGET := playwise-device-lab
BUILD := build
APP_TITLE := PlayWise DEVICE LAB
APP_AUTHOR := PlayWise internal
APP_VERSION := $(PLAYWISE_VERSION)-lab
CC := $(DEVKITA64)/bin/aarch64-none-elf-gcc
ELF2NRO := $(DEVKITPRO)/tools/bin/elf2nro
ARCH := -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -std=gnu99 -O2 -g -Wall -Wextra -Werror -ffunction-sections $(ARCH)
CFLAGS += -D__SWITCH__ -DPLAYWISE_DEVICE_LAB -I../.. -I../../build/device-lab/generated -I$(DEVKITPRO)/libnx/include
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(BUILD)/$(TARGET).map
LIBS := -L$(DEVKITPRO)/libnx/lib -lnx
NACP := $(TARGET).nacp
MANIFEST_HEADER := ../../build/device-lab/generated/release_manifest.h

.PHONY: all clean

all: $(TARGET).nro

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/main.o: main.c $(MANIFEST_HEADER) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(BUILD)/main.o
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

$(NACP): $(MAKEFILE_LIST)
	$(DEVKITPRO)/tools/bin/nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

$(TARGET).nro: $(TARGET).elf $(NACP)
	$(ELF2NRO) $< $@ --nacp=$(NACP)

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).nro $(NACP)
