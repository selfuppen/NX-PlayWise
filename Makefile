HOST_CC ?= gcc
HOST_CFLAGS ?= -std=c99 -Wall -Wextra -Werror -I.
HOST_BUILD_DIR := build/host
HOST_TEST := $(HOST_BUILD_DIR)/test_host_core
HOST_UI_TEST := $(HOST_BUILD_DIR)/test_ui_state
PACKAGE_TIMESTAMP ?= $(shell date +%Y%m%d-%H%M%S)

COMMON_SRCS := \
	common/crypto/sha256.c \
	common/protocol/error_code.c \
	common/protocol/request_schema.c \
	common/protocol/result_builder.c \
	common/token/token_v1.c \
	common/time/ptc_time.c \
	common/rules/rules.c \
	common/policy/control_policy.c

THIRD_PARTY_SRCS := \
	third_party/cjson/cJSON.c

PLATFORM_HOST_SRCS := \
	platform/host/mem_storage.c \
	platform/host/pctl_stub.c \
	platform/host/fake_time.c \
	platform/switch/play_timer_settings_layout.c

ORCH_SRCS := \
	sysmodule/sysmodule_core.c \
	companion/auth.c \
	companion/file_protocol.c \
	companion/request_client.c \
	companion/result_summary.c \
	companion/overlay/input_model.c \
	companion/overlay/bridge.c \
	companion/self_check.c

TEST_SRCS := tests/c/test_host_core.c
UI_TEST_SRCS := companion/nro/ui_state.c companion/file_protocol.c companion/request_client.c companion/result_summary.c common/protocol/result_builder.c common/protocol/error_code.c common/rules/rules.c common/time/ptc_time.c third_party/cjson/cJSON.c tests/c/test_ui_state.c

.PHONY: all test-host test-python test companion-nro sysmodule-nsp packages package-safe-nro clean package-disabled-boot2 package-observe-boot2 package-grant-boot2 package-enforce-boot2

all: test

$(HOST_BUILD_DIR):
	mkdir -p $(HOST_BUILD_DIR)

$(HOST_TEST): $(COMMON_SRCS) $(THIRD_PARTY_SRCS) $(PLATFORM_HOST_SRCS) $(ORCH_SRCS) $(TEST_SRCS) | $(HOST_BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $(COMMON_SRCS) $(THIRD_PARTY_SRCS) $(PLATFORM_HOST_SRCS) $(ORCH_SRCS) $(TEST_SRCS)

$(HOST_UI_TEST): $(UI_TEST_SRCS) companion/nro/ui_graphics.h | $(HOST_BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $(UI_TEST_SRCS)

test-host: $(HOST_TEST) $(HOST_UI_TEST)
	$(HOST_TEST)
	$(HOST_UI_TEST)

test-python:
	python3 tools/test.py

test: test-host test-python

companion-nro:
	$(MAKE) -C companion/nro
	mkdir -p build/switch
	cp companion/nro/pctc.nro build/switch/pctc.nro

companion-overlay:
	$(MAKE) -C companion/overlay
	mkdir -p build/switch
	cp companion/overlay/pctc.ovl build/switch/pctc.ovl

package-safe-nro: companion-nro
	python3 tools/package_sdmc.py --mode safe --out build/packages/safe-nro --zip build/packages/safe-nro-$(PACKAGE_TIMESTAMP).zip --nro build/switch/pctc.nro

sysmodule-nsp:
	$(MAKE) -C sysmodule
	mkdir -p build/switch
	cp sysmodule/pctc-sysmodule.nsp build/switch/exefs.nsp

package-disabled-boot2 package-observe-boot2 package-grant-boot2 package-enforce-boot2: sysmodule-nsp companion-nro companion-overlay
	python3 tools/package_sdmc.py --mode $(subst package-,,$(subst -boot2,,$@)) --out build/packages/$(subst package-,,$@) --zip build/packages/$(subst package-,,$@)-$(PACKAGE_TIMESTAMP).zip --sysmodule-exefs build/switch/exefs.nsp --nro build/switch/pctc.nro --overlay build/switch/pctc.ovl --boot2

packages: package-safe-nro package-disabled-boot2 package-observe-boot2 package-grant-boot2 package-enforce-boot2

clean:
	rm -rf build
	$(MAKE) -C companion/nro clean || true
	$(MAKE) -C companion/overlay clean || true
	$(MAKE) -C sysmodule clean || true
