HOST_CC ?= gcc
HOST_CFLAGS ?= -std=c99 -Wall -Wextra -Werror -I.
include common/version.mk
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
	common/token/token_v2.c \
	common/time/ptc_time.c \
	common/rules/rules.c \
	common/support/support_export.c \
	common/security/credential_policy.c \
	common/policy/control_policy.c

THIRD_PARTY_SRCS := \
	third_party/cjson/cJSON.c \
	third_party/qrcodegen/qrcodegen.c

PLATFORM_HOST_SRCS := \
	platform/host/mem_storage.c \
	platform/host/pctl_stub.c \
	platform/host/fake_time.c \
	platform/switch/play_timer_settings_layout.c

ORCH_SRCS := \
	sysmodule/sysmodule_core.c \
	companion/auth.c \
	companion/file_protocol.c \
	companion/transport_client.c \
	companion/request_client.c \
	companion/result_summary.c \
	companion/overlay/input_model.c \
	companion/overlay/bridge.c

TEST_SRCS := tests/c/test_host_core.c
UI_TEST_SRCS := companion/nro/ui_state.c companion/file_protocol.c companion/request_client.c companion/result_summary.c common/protocol/request_schema.c common/protocol/result_builder.c common/protocol/error_code.c common/rules/rules.c common/time/ptc_time.c third_party/cjson/cJSON.c tests/c/test_ui_state.c

.PHONY: all manifest device-lab-manifest test-host test-python test companion-nro companion-overlay sysmodule-nsp packages package-playwise device-lab-sysmodule device-lab-nro device-lab-package clean

all: test

manifest:
	mkdir -p build/generated
	python3 tools/generate_release_manifest.py --profile release --json build/generated/release-manifest.json --header build/generated/release_manifest.h

device-lab-manifest:
	mkdir -p build/device-lab/generated
	python3 tools/generate_release_manifest.py --profile device-lab --json build/device-lab/generated/release-manifest.json --header build/device-lab/generated/release_manifest.h

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

companion-nro: manifest
	$(MAKE) -C companion/nro
	mkdir -p build/switch
	cp companion/nro/pctc.nro build/switch/pctc.nro

companion-overlay: manifest
	$(MAKE) -C companion/overlay
	mkdir -p build/switch
	cp companion/overlay/pctc.ovl build/switch/pctc.ovl

sysmodule-nsp: manifest
	$(MAKE) -C sysmodule
	mkdir -p build/switch
	cp sysmodule/pctc-sysmodule.nsp build/switch/exefs.nsp
	$(DEVKITA64)/bin/aarch64-none-elf-objcopy -O binary sysmodule/pctc-sysmodule.elf build/switch/pctc-sysmodule.bin

package-playwise: sysmodule-nsp companion-nro companion-overlay
	python3 tools/package_sdmc.py --out build/packages/playwise --zip build/packages/playwise-$(PLAYWISE_VERSION).zip --manifest build/generated/release-manifest.json --sysmodule-exefs build/switch/exefs.nsp --nro build/switch/pctc.nro --overlay build/switch/pctc.ovl --boot2

packages: package-playwise

# Internal-only target. It is deliberately not a dependency of `packages` and
# its package omits boot2.flag so an operator must opt in on the device.
device-lab-sysmodule: device-lab-manifest
	$(MAKE) -C sysmodule TARGET=pwtl-sysmodule BUILD=build-device-lab CONFIG_JSON=sysmodule.device-lab.json MANIFEST_INCLUDE=../build/device-lab/generated DEFINES=-DPLAYWISE_DEVICE_LAB
	mkdir -p build/device-lab/switch
	cp sysmodule/pwtl-sysmodule.nsp build/device-lab/switch/exefs.nsp
	$(DEVKITA64)/bin/aarch64-none-elf-objcopy -O binary sysmodule/pwtl-sysmodule.elf build/device-lab/switch/pwtl-sysmodule.bin

device-lab-nro: device-lab-manifest
	$(MAKE) -C device_lab/nro
	mkdir -p build/device-lab/switch
	cp device_lab/nro/playwise-device-lab.nro build/device-lab/switch/playwise-device-lab.nro

device-lab-package: device-lab-sysmodule device-lab-nro
	python3 tools/package_device_lab.py --out build/device-lab/package --zip build/device-lab/playwise-device-lab-$(PLAYWISE_VERSION).zip --manifest build/device-lab/generated/release-manifest.json --sysmodule-exefs build/device-lab/switch/exefs.nsp --nro build/device-lab/switch/playwise-device-lab.nro

clean:
	rm -rf build
	$(MAKE) -C companion/nro clean || true
	$(MAKE) -C companion/overlay clean || true
	$(MAKE) -C sysmodule clean || true
	$(MAKE) -C device_lab/nro clean || true
