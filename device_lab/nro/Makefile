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
PKG_CONFIG := $(DEVKITPRO)/portlibs/switch/bin/aarch64-none-elf-pkg-config
FREETYPE_CFLAGS := $(shell $(PKG_CONFIG) --cflags freetype2)
FREETYPE_LIBS := $(shell $(PKG_CONFIG) --static --libs freetype2)
ARCH := -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -std=gnu99 -O2 -g -Wall -Wextra -Werror -ffunction-sections -MMD -MP $(ARCH)
CFLAGS += -D__SWITCH__ -DPLAYWISE_DEVICE_LAB -I../.. -I../../build/device-lab/generated -I$(DEVKITPRO)/libnx/include $(FREETYPE_CFLAGS)
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(BUILD)/$(TARGET).map
LIBS := -L$(DEVKITPRO)/libnx/lib $(FREETYPE_LIBS) -lnx
NACP := $(TARGET).nacp
MANIFEST_HEADER := ../../build/device-lab/generated/release_manifest.h

.PHONY: all clean

all: $(TARGET).nro

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/main.o: main.c $(MANIFEST_HEADER) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_graphics.o: ui_graphics.c ui_graphics.h ../ui_model.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_model.o: ../ui_model.c ../ui_model.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot_flags.o: ../boot_flags.c ../boot_flags.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/hot_switch.o: hot_switch.c hot_switch.h ../boot_flags.h $(MANIFEST_HEADER) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/handoff_guard.o: ../handoff_guard.c ../handoff_guard.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(BUILD)/main.o $(BUILD)/ui_graphics.o $(BUILD)/ui_model.o $(BUILD)/boot_flags.o $(BUILD)/hot_switch.o $(BUILD)/handoff_guard.o
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

LAB_OBJS := $(BUILD)/main.o $(BUILD)/ui_graphics.o $(BUILD)/ui_model.o $(BUILD)/boot_flags.o $(BUILD)/hot_switch.o $(BUILD)/handoff_guard.o
LAB_DEPENDS := $(LAB_OBJS:.o=.d)
$(LAB_OBJS): Makefile ../../common/version.mk $(MANIFEST_HEADER)
-include $(LAB_DEPENDS)

$(NACP): $(MAKEFILE_LIST)
	$(DEVKITPRO)/tools/bin/nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

$(TARGET).nro: $(TARGET).elf $(NACP)
	$(ELF2NRO) $< $@ --nacp=$(NACP)

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).nro $(NACP)
