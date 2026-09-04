HOST_CC ?= gcc
HOST_CFLAGS ?= -std=c99 -Wall -Wextra -Werror -I.
include common/version.mk
HOST_BUILD_DIR := build/host
HOST_TEST := $(HOST_BUILD_DIR)/test_host_core
HOST_UI_TEST := $(HOST_BUILD_DIR)/test_ui_state
HOST_LAB_TEST := $(HOST_BUILD_DIR)/test_device_lab
PACKAGE_TIMESTAMP ?= $(shell date +%Y%m%d-%H%M%S)

COMMON_SRCS := \
	common/crypto/sha256.c \
	common/protocol/error_code.c \
	common/protocol/activity_history.c \
	common/protocol/redemption_history.c \
	common/protocol/request_schema.c \
	common/protocol/result_builder.c \
	common/token/token_v1.c \
	common/token/token_v2.c \
	common/time/ptc_time.c \
	common/usage/daily_summary.c \
	common/rules/rules.c \
	common/rules/holiday_calendar.c \
	common/support/support_export.c \
	common/security/credential_policy.c \
	common/policy/control_policy.c

THIRD_PARTY_SRCS := \
	third_party/cjson/cJSON.c \
	third_party/qrcodegen/qrcodegen.c

PLATFORM_HOST_SRCS := \
	platform/install_defaults.c \
	platform/host/mem_storage.c \
	platform/host/pctl_stub.c \
	platform/host/fake_time.c \
	platform/switch/usage_stats_adapter.c \
	platform/switch/play_timer_settings_layout.c

ORCH_SRCS := \
	companion/album_restriction.c \
	sysmodule/sysmodule_core.c \
	companion/auth.c \
	companion/file_protocol.c \
	companion/transport_client.c \
	companion/request_client.c \
	companion/result_summary.c \
	companion/overlay/input_model.c \
	companion/overlay/bridge.c

TEST_SRCS := tests/c/test_host_core.c
UI_TEST_SRCS := companion/nro/ui_state.c companion/nro/ui_theme.c companion/file_protocol.c companion/request_client.c companion/result_summary.c common/protocol/activity_history.c common/protocol/redemption_history.c common/protocol/request_schema.c common/protocol/result_builder.c common/protocol/error_code.c common/rules/rules.c common/rules/holiday_calendar.c common/time/ptc_time.c common/usage/daily_summary.c third_party/cjson/cJSON.c tests/c/test_ui_state.c

.PHONY: all manifest device-lab-manifest eden-test-manifest test-host test-python test companion-nro companion-overlay sysmodule-nsp eden-test-nro packages package-playwise package-complete device-lab-sysmodule device-lab-nro device-lab-overlay device-lab-package clean

all: test

manifest:
	mkdir -p build/generated
	python3 tools/generate_release_manifest.py --profile release --json build/generated/release-manifest.json --header build/generated/release_manifest.h

device-lab-manifest:
	mkdir -p build/device-lab/generated
	python3 tools/generate_release_manifest.py --profile device-lab --json build/device-lab/generated/release-manifest.json --header build/device-lab/generated/release_manifest.h

eden-test-manifest:
	mkdir -p build/eden-test/generated
	python3 tools/generate_release_manifest.py --profile eden-test --json build/eden-test/generated/release-manifest.json --header build/eden-test/generated/release_manifest.h

$(HOST_BUILD_DIR):
	mkdir -p $(HOST_BUILD_DIR)

$(HOST_TEST): $(COMMON_SRCS) $(THIRD_PARTY_SRCS) $(PLATFORM_HOST_SRCS) $(ORCH_SRCS) $(TEST_SRCS) | $(HOST_BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $(COMMON_SRCS) $(THIRD_PARTY_SRCS) $(PLATFORM_HOST_SRCS) $(ORCH_SRCS) $(TEST_SRCS)

$(HOST_UI_TEST): $(UI_TEST_SRCS) companion/nro/ui_graphics.h | $(HOST_BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $(UI_TEST_SRCS) -lm

$(HOST_LAB_TEST): common/crypto/sha256.c common/protocol/atmosphere_version.c common/protocol/error_code.c common/protocol/request_schema.c common/protocol/result_builder.c common/time/ptc_time.c common/rules/rules.c common/rules/holiday_calendar.c platform/host/mem_storage.c platform/host/pctl_stub.c platform/host/fake_time.c platform/switch/play_timer_settings_layout.c sysmodule/lab_session.c device_lab/boot_flags.c device_lab/handoff_guard.c device_lab/ui_model.c tests/c/test_device_lab.c | $(HOST_BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -DPLAYWISE_DEVICE_LAB -o $@ $^

test-host: $(HOST_TEST) $(HOST_UI_TEST) $(HOST_LAB_TEST)
	$(HOST_TEST)
	$(HOST_UI_TEST)
	$(HOST_LAB_TEST)

# Optional visual QA with a local, untracked font; nothing enters release Zips.
ifneq ($(wildcard build/ui-preview-font.ttf),)
UI_PREVIEW_SRCS := $(filter-out tests/c/test_ui_state.c,$(UI_TEST_SRCS)) third_party/qrcodegen/qrcodegen.c common/security/credential_policy.c companion/album_restriction.c tests/ui_preview/render.c
$(HOST_BUILD_DIR)/ui_preview: $(UI_PREVIEW_SRCS) companion/nro/ui_graphics.c $(wildcard tests/ui_preview/*.h) | $(HOST_BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -Itests/ui_preview -o $@ $(UI_PREVIEW_SRCS) -lm

.PHONY: ui-previews
ui-previews: $(HOST_BUILD_DIR)/ui_preview
	mkdir -p build/ui-previews
	$(HOST_BUILD_DIR)/ui_preview build/ui-preview-font.ttf build/ui-previews
	python3 tools/convert_ui_previews.py

test-host: ui-previews
endif

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
	cp companion/overlay/playwise.ovl build/switch/playwise.ovl

# Emulator-only NRO. It embeds the production core with a simulated PCTL adapter
# and its own app root, and is deliberately excluded from every package target.
eden-test-nro: eden-test-manifest
	$(MAKE) -C companion/nro TARGET=pctc-eden BUILD=build-eden APP_TITLE="PlayWise Eden Test" APP_AUTHOR="PlayWise internal" MANIFEST_INCLUDE=../../build/eden-test/generated DEFINES=-DPLAYWISE_EDEN EDEN_BUILD=1
	mkdir -p build/eden-test
	cp companion/nro/pctc-eden.nro build/eden-test/pctc-eden.nro

sysmodule-nsp: manifest
	$(MAKE) -C sysmodule
	mkdir -p build/switch
	cp sysmodule/pctc-sysmodule.nsp build/switch/exefs.nsp
	$(DEVKITA64)/bin/aarch64-none-elf-objcopy -O binary sysmodule/pctc-sysmodule.elf build/switch/pctc-sysmodule.bin

package-playwise: sysmodule-nsp companion-nro companion-overlay
	python3 tools/package_sdmc.py --out build/packages/playwise --zip build/packages/playwise-$(PLAYWISE_VERSION).zip --manifest build/generated/release-manifest.json --sysmodule-exefs build/switch/exefs.nsp --nro build/switch/pctc.nro --overlay build/switch/playwise.ovl --boot2

package-complete: package-playwise
	python3 tools/build_ptc_standalone.py --check
	python3 tools/package_delivery.py --standard-package build/packages/playwise-$(PLAYWISE_VERSION).zip --offline-html tools/ptc_frontend/playwise-offline.html --output build/packages/playwise-complete-$(PLAYWISE_VERSION).zip

packages: package-complete device-lab-package

# Internal test target. Built alongside release packages but omits boot2.flag
# so an operator must opt in on the device.
device-lab-sysmodule: device-lab-manifest
	$(MAKE) -C sysmodule TARGET=pwtl-sysmodule BUILD=build-device-lab CONFIG_JSON=sysmodule.device-lab.json MANIFEST_INCLUDE=../build/device-lab/generated DEFINES=-DPLAYWISE_DEVICE_LAB
	mkdir -p build/device-lab/switch
	cp sysmodule/pwtl-sysmodule.nsp build/device-lab/switch/exefs.nsp
	$(DEVKITA64)/bin/aarch64-none-elf-objcopy -O binary sysmodule/pwtl-sysmodule.elf build/device-lab/switch/pwtl-sysmodule.bin

device-lab-nro: device-lab-manifest
	$(MAKE) -C device_lab/nro
	mkdir -p build/device-lab/switch
	cp device_lab/nro/playwise-device-lab.nro build/device-lab/switch/playwise-device-lab.nro

device-lab-overlay: device-lab-manifest
	$(MAKE) -C device_lab/overlay
	mkdir -p build/device-lab/switch
	cp device_lab/overlay/playwise-device-lab.ovl build/device-lab/switch/playwise-device-lab.ovl

device-lab-package: device-lab-sysmodule device-lab-nro device-lab-overlay
	python3 tools/package_device_lab.py --out build/packages/playwise-device-lab --zip build/packages/playwise-device-lab-$(PLAYWISE_VERSION).zip --manifest build/device-lab/generated/release-manifest.json --sysmodule-exefs build/device-lab/switch/exefs.nsp --nro build/device-lab/switch/playwise-device-lab.nro --overlay build/device-lab/switch/playwise-device-lab.ovl

clean:
	rm -rf build/host build/generated build/switch build/packages build/device-lab
	@if [ "$(CLEAN_EDEN)" = "1" ]; then rm -rf build/eden-test; fi
	$(MAKE) -C companion/nro clean || true
	rm -rf companion/nro/build-eden companion/nro/pctc-eden.elf companion/nro/pctc-eden.nro companion/nro/pctc-eden.nacp
	$(MAKE) -C companion/overlay clean || true
	$(MAKE) -C sysmodule clean || true
	$(MAKE) -C device_lab/nro clean || true
	$(MAKE) -C device_lab/overlay clean || true
