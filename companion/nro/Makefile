ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set. Source devkitPro environment before building the Companion NRO)
endif

ifeq ($(strip $(DEVKITA64)),)
$(error DEVKITA64 is not set. Source devkitPro environment before building the Companion NRO)
endif

TARGET := pctc
BUILD := build
include ../../common/version.mk
APP_TITLE := 任我玩
APP_AUTHOR := selfuppen@qq.com
APP_VERSION := $(PLAYWISE_VERSION)
APP_ICON := $(CURDIR)/icon.png

CC := $(DEVKITA64)/bin/aarch64-none-elf-gcc
ELF2NRO := $(DEVKITPRO)/tools/bin/elf2nro
PKG_CONFIG := $(DEVKITPRO)/portlibs/switch/bin/aarch64-none-elf-pkg-config
FREETYPE_CFLAGS := $(shell $(PKG_CONFIG) --cflags freetype2)
FREETYPE_LIBS := $(shell $(PKG_CONFIG) --static --libs freetype2)

ARCH := -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -std=gnu99 -O2 -g -Wall -Wextra -Werror -ffunction-sections $(ARCH)
CFLAGS += -D__SWITCH__ -I../.. -I../../build/generated -I$(DEVKITPRO)/libnx/include $(FREETYPE_CFLAGS)
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(BUILD)/$(TARGET).map
LIBS := -L$(DEVKITPRO)/libnx/lib $(FREETYPE_LIBS) -lnx

OBJS := \
	$(BUILD)/main.o \
	$(BUILD)/album_restriction.o \
	$(BUILD)/ui_graphics.o \
	$(BUILD)/ui_state.o \
	$(BUILD)/auth.o \
	$(BUILD)/file_protocol.o \
	$(BUILD)/transport_client.o \
	$(BUILD)/switch_ipc_client.o \
	$(BUILD)/request_client.o \
	$(BUILD)/result_summary.o \
	$(BUILD)/sha256.o \
	$(BUILD)/rules.o \
	$(BUILD)/holiday_calendar.o \
	$(BUILD)/support_export.o \
	$(BUILD)/credential_policy.o \
	$(BUILD)/token_v2.o \
	$(BUILD)/qrcodegen.o \
	$(BUILD)/ptc_time.o \
	$(BUILD)/result_builder.o \
	$(BUILD)/error_code.o \
	$(BUILD)/request_schema.o \
	$(BUILD)/cJSON.o \
	$(BUILD)/fs_storage.o

.PHONY: all clean

NACP := $(TARGET).nacp

all: $(TARGET).nro

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/main.o: main.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/album_restriction.o: ../album_restriction.c ../album_restriction.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_graphics.o: ui_graphics.c ui_graphics.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_state.o: ui_state.c ui_graphics.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/file_protocol.o: ../file_protocol.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/auth.o: ../auth.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/request_client.o: ../request_client.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/transport_client.o: ../transport_client.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/switch_ipc_client.o: ../switch_ipc_client.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/result_summary.o: ../result_summary.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sha256.o: ../../common/crypto/sha256.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/rules.o: ../../common/rules/rules.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/holiday_calendar.o: ../../common/rules/holiday_calendar.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/support_export.o: ../../common/support/support_export.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/credential_policy.o: ../../common/security/credential_policy.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/token_v2.o: ../../common/token/token_v2.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/qrcodegen.o: ../../third_party/qrcodegen/qrcodegen.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ptc_time.o: ../../common/time/ptc_time.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/result_builder.o: ../../common/protocol/result_builder.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/error_code.o: ../../common/protocol/error_code.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/request_schema.o: ../../common/protocol/request_schema.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/cJSON.o: ../../third_party/cjson/cJSON.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/fs_storage.o: ../../platform/switch/fs_storage.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

$(NACP): $(MAKEFILE_LIST)
	$(DEVKITPRO)/tools/bin/nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

$(TARGET).nro: $(TARGET).elf $(NACP) $(APP_ICON)
	$(ELF2NRO) $< $@ --icon=$(APP_ICON) --nacp=$(NACP)

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).nro $(NACP)
