#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set. Source devkitPro environment before building the Device Lab overlay)
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules
include $(TOPDIR)/../../common/version.mk

TARGET := playwise-device-lab
BUILD := build
SOURCES := source .. ../../companion ../../common/protocol ../../common/time ../../common/rules ../../platform/switch ../../third_party/cjson
INCLUDES := ../../companion/overlay/vendor/libtesla/include ../.. ../../common ../../platform ../../platform/switch ../../build/device-lab/generated
APP_TITLE := PlayWise Device Lab
APP_AUTHOR := PlayWise internal
APP_VERSION := $(PLAYWISE_VERSION)-lab
NO_ICON := 1

export NROFLAGS += --nacp=$(TOPDIR)/$(TARGET).nacp

ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -g -O2 -Wall -Wextra -Werror -ffunction-sections $(ARCH) $(DEFINES)
CFLAGS += $(INCLUDE) -D__SWITCH__ -DPLAYWISE_DEVICE_LAB -DPTC_IPC_CLIENT_OVERLAY
CXXFLAGS := $(filter-out -Werror,$(CFLAGS)) -std=c++20 -fno-exceptions -fno-rtti -DTESLA_FORCE_CHINESE_SIMPLIFIED_FONT \
	-Wno-missing-field-initializers -Wno-unused-parameter -Wno-ignored-qualifiers
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS := -lnx
LIBDIRS := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
CPPFILES := main.cpp
CFILES := ui_model.c file_protocol.c transport_client.c request_client.c request_schema.c result_builder.c error_code.c ptc_time.c rules.c fs_storage.c cJSON.c
export LD := $(CXX)
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export OFILES := $(OFILES_SRC)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
.PHONY: all clean $(BUILD)
all: $(BUILD)
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $@ -f $(CURDIR)/Makefile
clean:
	@rm -fr $(BUILD) $(TARGET).ovl $(TARGET).nacp $(TARGET).elf
else
DEPENDS := $(OFILES:.o=.d)
all: $(OUTPUT).ovl
$(OUTPUT).ovl: $(OUTPUT).elf $(OUTPUT).nacp
	@elf2nro $< $@ $(NROFLAGS)
$(OUTPUT).elf: $(OFILES)
%.cpp.o: %.cpp
	@$(CXX) $(CXXFLAGS) $(INCLUDE) -MMD -MP -c $< -o $@
%.c.o: %.c
	@$(CC) $(CFLAGS) $(INCLUDE) -MMD -MP -c $< -o $@
-include $(DEPENDS)
endif
