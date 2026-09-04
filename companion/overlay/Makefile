#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set. Source devkitPro environment before building the Tesla overlay)
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules
include $(TOPDIR)/../../common/version.mk

TARGET := playwise
BUILD := build
SOURCES := source . .. ../.. ../../common/protocol ../../common/time ../../platform/switch ../../third_party/cjson
INCLUDES := vendor/libtesla/include ../.. ../../common ../../platform ../../platform/switch ../../build/generated
APP_TITLE := 任我玩
APP_AUTHOR := selfuppen@qq.com
APP_VERSION := $(PLAYWISE_VERSION)
NO_ICON := 1

export NROFLAGS += --nacp=$(TOPDIR)/$(TARGET).nacp

ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -g -O2 -Wall -Wextra -Werror -ffunction-sections $(ARCH) $(DEFINES)
CFLAGS += $(INCLUDE) -D__SWITCH__ -DPTC_IPC_CLIENT_OVERLAY
CXXFLAGS := $(filter-out -Werror,$(CFLAGS)) -std=c++20 -fno-exceptions -fno-rtti \
	-Wno-missing-field-initializers -Wno-unused-parameter -Wno-ignored-qualifiers
CXXFLAGS += -Wno-type-limits
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS := -lnx
LIBDIRS := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
CPPFILES := main.cpp
CFILES := input_model.c bridge.c file_protocol.c transport_client.c switch_ipc_client.c request_client.c result_summary.c request_schema.c result_builder.c error_code.c ptc_time.c fs_storage.c cJSON.c
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
	@rm -fr $(BUILD) $(TARGET).ovl $(TARGET).nacp $(TARGET).elf pctc.ovl pctc.nacp pctc.elf
else
DEPENDS := $(OFILES:.o=.d)
all: $(OUTPUT).ovl
$(OFILES): $(TOPDIR)/Makefile $(TOPDIR)/../../common/version.mk $(TOPDIR)/../../build/generated/release_manifest.h
$(OUTPUT).ovl: $(OUTPUT).elf $(OUTPUT).nacp
	@elf2nro $< $@ $(NROFLAGS)
$(OUTPUT).elf: $(OFILES)
%.cpp.o: %.cpp
	@$(CXX) $(CXXFLAGS) $(INCLUDE) -MMD -MP -c $< -o $@
%.c.o: %.c
	@$(CC) $(CFLAGS) $(INCLUDE) -MMD -MP -c $< -o $@
-include $(DEPENDS)
endif
