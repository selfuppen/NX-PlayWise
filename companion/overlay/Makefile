#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set. Source devkitPro environment before building the Tesla overlay)
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

TARGET := pctc
BUILD := build
SOURCES := source . .. ../.. ../../common/protocol ../../platform/switch ../../third_party/cjson
INCLUDES := vendor/libtesla/include ../.. ../../common ../../platform ../../platform/switch
NO_ICON := 1
NO_NACP := 1

ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -g -O2 -Wall -Wextra -Werror -ffunction-sections $(ARCH) $(DEFINES)
CFLAGS += $(INCLUDE) -D__SWITCH__
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
CFILES := input_model.c bridge.c file_protocol.c request_client.c result_summary.c result_builder.c error_code.c fs_storage.c cJSON.c
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
	@rm -fr $(BUILD) $(TARGET).ovl $(TARGET).elf
else
DEPENDS := $(OFILES:.o=.d)
all: $(OUTPUT).ovl
$(OUTPUT).ovl: $(OUTPUT).elf
	@elf2nro $< $@ $(NROFLAGS)
$(OUTPUT).elf: $(OFILES)
%.cpp.o: %.cpp
	@$(CXX) $(CXXFLAGS) $(INCLUDE) -MMD -MP -c $< -o $@
%.c.o: %.c
	@$(CC) $(CFLAGS) $(INCLUDE) -MMD -MP -c $< -o $@
-include $(DEPENDS)
endif
