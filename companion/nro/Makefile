ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set. Source devkitPro environment before building the Companion NRO)
endif

ifeq ($(strip $(DEVKITA64)),)
$(error DEVKITA64 is not set. Source devkitPro environment before building the Companion NRO)
endif

TARGET := pctc
BUILD := build

CC := $(DEVKITA64)/bin/aarch64-none-elf-gcc
ELF2NRO := $(DEVKITPRO)/tools/bin/elf2nro

ARCH := -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -std=c99 -O2 -g -Wall -Wextra -Werror -ffunction-sections $(ARCH)
CFLAGS += -D__SWITCH__ -I../.. -I$(DEVKITPRO)/libnx/include
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(BUILD)/$(TARGET).map
LIBS := -L$(DEVKITPRO)/libnx/lib -lnx

OBJS := \
	$(BUILD)/main.o \
	$(BUILD)/auth.o \
	$(BUILD)/file_protocol.o \
	$(BUILD)/request_client.o \
	$(BUILD)/self_check.o \
	$(BUILD)/sha256.o \
	$(BUILD)/result_builder.o \
	$(BUILD)/error_code.o \
	$(BUILD)/cJSON.o \
	$(BUILD)/fs_storage.o

.PHONY: all clean

all: $(TARGET).nro

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/main.o: main.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/file_protocol.o: ../file_protocol.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/auth.o: ../auth.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/request_client.o: ../request_client.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/self_check.o: ../self_check.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sha256.o: ../../common/crypto/sha256.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/result_builder.o: ../../common/protocol/result_builder.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/error_code.o: ../../common/protocol/error_code.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/cJSON.o: ../../third_party/cjson/cJSON.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/fs_storage.o: ../../platform/switch/fs_storage.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

$(TARGET).nro: $(TARGET).elf
	$(ELF2NRO) $< $@

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).nro
