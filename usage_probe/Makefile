ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set)
endif
TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules
TARGET := usage-probe
BUILD := build
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -std=gnu11 -g -O2 -Wall -Wextra -Werror -ffunction-sections $(ARCH)
CFLAGS += -D__SWITCH__ -DPLAYWISE_DEVICE_LAB $(INCLUDE)
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH)
LIBS := -lnx
ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(CURDIR):$(CURDIR)/../platform/switch:$(CURDIR)/../common/time:$(CURDIR)/../third_party/cjson
export DEPSDIR := $(CURDIR)/$(BUILD)
export OFILES := main.o pctl_adapter.o play_timer_settings_layout.o ptc_time.o cJSON.o
export INCLUDE := -I$(CURDIR)/.. -I$(DEVKITPRO)/libnx/include -I$(CURDIR)/../build/usage-probe/generated
export LIBPATHS := -L$(DEVKITPRO)/libnx/lib
export APP_JSON := $(CURDIR)/probe.json
export LD := $(CC)
.PHONY: all clean $(BUILD)
all: $(BUILD) launcher.nro
$(BUILD):
	mkdir -p $@
	$(MAKE) --no-print-directory -C $@ -f $(CURDIR)/Makefile
launcher.elf: launcher.c ../build/usage-probe/generated/probe_identity.h
	$(CC) $(CFLAGS) -I$(DEVKITPRO)/libnx/include -I../build/usage-probe/generated $(LDFLAGS) $< -L$(DEVKITPRO)/libnx/lib -lnx -o $@
launcher.nacp:
	$(DEVKITPRO)/tools/bin/nacptool --create "PlayWise Usage Probe" "PlayWise internal" "1.0" $@
launcher.nro: launcher.elf launcher.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=launcher.nacp
clean:
	rm -rf build usage-probe.elf usage-probe.nso usage-probe.npdm usage-probe.nsp launcher.elf launcher.nro launcher.nacp
else
DEPENDS := $(OFILES:.o=.d)
all: $(OUTPUT).nsp
$(OUTPUT).nsp: $(OUTPUT).nso $(OUTPUT).npdm
$(OUTPUT).nso: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)
$(OFILES): $(TOPDIR)/Makefile $(TOPDIR)/../build/usage-probe/generated/probe_identity.h
-include $(DEPENDS)
endif
