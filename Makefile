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
	common/security/credential_policy.c

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
	companion/hot_reload_guard.c \
	sysmodule/sysmodule_storage.c \
	sysmodule/sysmodule_audit.c \
	sysmodule/sysmodule_history.c \
	sysmodule/sysmodule_results.c \
	sysmodule/sysmodule_control.c \
	sysmodule/sysmodule_setup.c \
	sysmodule/sysmodule_requests.c \
	sysmodule/sysmodule_request_grants.c \
	sysmodule/sysmodule_request_recovery.c \
	sysmodule/sysmodule_core.c \
	companion/auth.c \
	companion/file_protocol.c \
	companion/transport_client.c \
	companion/request_client.c \
	companion/result_summary.c \
	companion/overlay/input_model.c \
	companion/overlay/bridge.c

TEST_SRCS := tests/c/test_host_core.c
UI_STATE_SRCS := companion/nro/ui_state.c companion/nro/ui_plan_state.c companion/nro/ui_navigation_state.c companion/nro/ui_status_state.c companion/nro/ui_input_state.c companion/nro/ui_result_state.c companion/nro/ui_layout.c companion/nro/ui_hit_test.c
UI_RENDER_SRCS := companion/nro/ui_graphics.c companion/nro/ui_render_primitives.c companion/nro/ui_render_text.c companion/nro/ui_render_core.c companion/nro/ui_render_components.c companion/nro/ui_render_action_card.c companion/nro/ui_render_child.c companion/nro/ui_render_setup.c companion/nro/ui_render_plan.c companion/nro/ui_render_pages.c companion/nro/ui_render_dialogs.c companion/nro/ui_render_dialog_policy.c companion/nro/ui_render_dialog_input.c companion/nro/ui_render_overlay_account.c companion/nro/ui_render_overlay_plan.c companion/nro/ui_render_overlay_support.c companion/nro/ui_render_overlays.c
UI_TEST_SRCS := $(UI_STATE_SRCS) companion/nro/ui_theme.c companion/file_protocol.c companion/request_client.c companion/result_summary.c common/protocol/activity_history.c common/protocol/redemption_history.c common/protocol/request_schema.c common/protocol/result_builder.c common/protocol/error_code.c common/rules/rules.c common/rules/holiday_calendar.c common/time/ptc_time.c common/usage/daily_summary.c third_party/cjson/cJSON.c tests/c/test_ui_state.c

STAGE_TIMER ?= python3 tools/stage_timer.py

.PHONY: all manifest device-lab-manifest eden-test-manifest test-host test-python test ui-previews companion-nro companion-overlay sysmodule-nsp eden-test-nro packages package-playwise package-complete device-lab-sysmodule device-lab-nro device-lab-overlay device-lab-package clean FORCE_HOST_REBUILD

all: test

manifest:
	mkdir -p build/generated
	$(STAGE_TIMER) playwise manifest -- python3 tools/generate_release_manifest.py --profile release --json build/generated/release-manifest.json --header build/generated/release_manifest.h

device-lab-manifest:
	mkdir -p build/device-lab/generated
	$(STAGE_TIMER) device-lab manifest -- python3 tools/generate_release_manifest.py --profile device-lab --json build/device-lab/generated/release-manifest.json --header build/device-lab/generated/release_manifest.h

eden-test-manifest:
	mkdir -p build/eden-test/generated
	$(STAGE_TIMER) eden-test manifest -- python3 tools/generate_release_manifest.py --profile eden-test --json build/eden-test/generated/release-manifest.json --header build/eden-test/generated/release_manifest.h

$(HOST_BUILD_DIR):
	mkdir -p $(HOST_BUILD_DIR)

FORCE_HOST_REBUILD:

$(HOST_TEST): $(COMMON_SRCS) $(THIRD_PARTY_SRCS) $(PLATFORM_HOST_SRCS) $(ORCH_SRCS) $(TEST_SRCS) FORCE_HOST_REBUILD | $(HOST_BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $(COMMON_SRCS) $(THIRD_PARTY_SRCS) $(PLATFORM_HOST_SRCS) $(ORCH_SRCS) $(TEST_SRCS)

$(HOST_UI_TEST): $(UI_TEST_SRCS) companion/nro/ui_model.h companion/nro/ui_state.h companion/nro/ui_layout.h FORCE_HOST_REBUILD | $(HOST_BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $(UI_TEST_SRCS) -lm

$(HOST_LAB_TEST): common/crypto/sha256.c common/protocol/atmosphere_version.c common/protocol/error_code.c common/protocol/request_schema.c common/protocol/result_builder.c common/time/ptc_time.c common/rules/rules.c common/rules/holiday_calendar.c platform/host/mem_storage.c platform/host/pctl_stub.c platform/host/fake_time.c platform/switch/play_timer_settings_layout.c sysmodule/lab_session.c sysmodule/lab_session_report.c device_lab/boot_flags.c device_lab/handoff_guard.c device_lab/ui_model.c tests/c/test_device_lab.c FORCE_HOST_REBUILD | $(HOST_BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -DPLAYWISE_DEVICE_LAB -o $@ $(filter-out FORCE_HOST_REBUILD,$^)

test-host: $(HOST_TEST) $(HOST_UI_TEST) $(HOST_LAB_TEST)
	$(STAGE_TIMER) global test-host -- sh -c '$(HOST_TEST) && $(HOST_UI_TEST) && $(HOST_LAB_TEST)'

UI_PREVIEW_SRCS := $(filter-out tests/c/test_ui_state.c,$(UI_TEST_SRCS)) third_party/qrcodegen/qrcodegen.c common/security/credential_policy.c companion/album_restriction.c tests/ui_preview/preview_font.c tests/ui_preview/render.c
$(HOST_BUILD_DIR)/ui_preview: $(UI_PREVIEW_SRCS) $(UI_RENDER_SRCS) companion/nro/ui_graphics.h companion/nro/ui_render_internal.h $(wildcard tests/ui_preview/*.h) FORCE_HOST_REBUILD | $(HOST_BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -D_POSIX_C_SOURCE=200809L -DPTC_UI_PREVIEW_ANIM_CLOCK_MS=1024000 -DPTC_UI_PREVIEW_WALL_TIME=1000 -Itests/ui_preview -o $@ $(UI_PREVIEW_SRCS) $(UI_RENDER_SRCS) -lm

.PHONY: test-ui-primitives
test-ui-primitives: $(HOST_BUILD_DIR)/ui_preview
	$(HOST_BUILD_DIR)/ui_preview --check-primitives

test-host: test-ui-primitives

# UI preview generation with the pinned, redistributable documentation font.
.PHONY: ui-previews
ui-previews: $(HOST_BUILD_DIR)/ui_preview
	@if [ ! -f third_party/fonts/noto-sans-sc/NotoSansSC-Regular.ttf ]; then \
		echo "ERROR: missing pinned preview font" >&2; \
		exit 1; \
	fi
	rm -rf build/ui-previews
	mkdir -p build/ui-previews
	$(STAGE_TIMER) playwise ui-previews -- $(HOST_BUILD_DIR)/ui_preview third_party/fonts/noto-sans-sc/NotoSansSC-Regular.ttf build/ui-previews
	$(STAGE_TIMER) playwise convert-previews -- python3 tools/convert_ui_previews.py

test-host: ui-previews

test-python:
	$(STAGE_TIMER) global test-python -- python3 tools/test.py

test: test-host test-python

companion-nro: manifest
	$(STAGE_TIMER) playwise nro -- $(MAKE) -C companion/nro
	mkdir -p build/switch
	cp companion/nro/pctc.nro build/switch/pctc.nro

companion-overlay: manifest
	$(STAGE_TIMER) playwise overlay -- $(MAKE) -C companion/overlay
	mkdir -p build/switch
	cp companion/overlay/playwise.ovl build/switch/playwise.ovl

# Emulator-only NRO. It embeds the production core with a simulated PCTL adapter
# and its own app root, and is deliberately excluded from every package target.
eden-test-nro: eden-test-manifest
	$(STAGE_TIMER) eden-test nro -- $(MAKE) -C companion/nro TARGET=pctc-eden BUILD=build-eden APP_TITLE="PlayWise Eden Test" APP_AUTHOR="PlayWise internal" MANIFEST_INCLUDE=../../build/eden-test/generated DEFINES=-DPLAYWISE_EDEN EDEN_BUILD=1
	mkdir -p build/eden-test
	cp companion/nro/pctc-eden.nro build/eden-test/pctc-eden.nro

sysmodule-nsp: manifest
	$(STAGE_TIMER) playwise sysmodule -- sh -c '$(MAKE) -C sysmodule && mkdir -p build/switch && cp sysmodule/pctc-sysmodule.nsp build/switch/exefs.nsp && $(DEVKITA64)/bin/aarch64-none-elf-objcopy -O binary sysmodule/pctc-sysmodule.elf build/switch/pctc-sysmodule.bin'

package-playwise: sysmodule-nsp companion-nro companion-overlay
	$(STAGE_TIMER) playwise package-zip -- python3 tools/package_sdmc.py --out build/packages/playwise --zip build/packages/playwise-$(PLAYWISE_VERSION).zip --manifest build/generated/release-manifest.json --sysmodule-exefs build/switch/exefs.nsp --nro build/switch/pctc.nro --overlay build/switch/playwise.ovl --boot2

package-complete: package-playwise
	$(STAGE_TIMER) playwise-complete offline-html -- python3 tools/build_ptc_standalone.py --check
	$(STAGE_TIMER) playwise-complete package-zip -- python3 tools/package_delivery.py --standard-package build/packages/playwise-$(PLAYWISE_VERSION).zip --offline-html tools/ptc_frontend/playwise-offline.html --output build/packages/playwise-complete-$(PLAYWISE_VERSION).zip

packages: package-complete device-lab-package

# Internal test target. Built alongside release packages but omits boot2.flag
# so an operator must opt in on the device.
device-lab-sysmodule: device-lab-manifest
	$(STAGE_TIMER) device-lab sysmodule -- sh -c '$(MAKE) -C sysmodule TARGET=pwtl-sysmodule BUILD=build-device-lab CONFIG_JSON=sysmodule.device-lab.json MANIFEST_INCLUDE=../build/device-lab/generated DEFINES=-DPLAYWISE_DEVICE_LAB && mkdir -p build/device-lab/switch && cp sysmodule/pwtl-sysmodule.nsp build/device-lab/switch/exefs.nsp && $(DEVKITA64)/bin/aarch64-none-elf-objcopy -O binary sysmodule/pwtl-sysmodule.elf build/device-lab/switch/pwtl-sysmodule.bin'

device-lab-nro: device-lab-manifest
	$(STAGE_TIMER) device-lab nro -- $(MAKE) -C device_lab/nro
	mkdir -p build/device-lab/switch
	cp device_lab/nro/playwise-device-lab.nro build/device-lab/switch/playwise-device-lab.nro

device-lab-overlay: device-lab-manifest
	$(STAGE_TIMER) device-lab overlay -- $(MAKE) -C device_lab/overlay
	mkdir -p build/device-lab/switch
	cp device_lab/overlay/playwise-device-lab.ovl build/device-lab/switch/playwise-device-lab.ovl

device-lab-package: device-lab-sysmodule device-lab-nro device-lab-overlay
	$(STAGE_TIMER) device-lab package-zip -- python3 tools/package_device_lab.py --out build/packages/playwise-device-lab --zip build/packages/playwise-device-lab-$(PLAYWISE_VERSION).zip --manifest build/device-lab/generated/release-manifest.json --sysmodule-exefs build/device-lab/switch/exefs.nsp --nro build/device-lab/switch/playwise-device-lab.nro --overlay build/device-lab/switch/playwise-device-lab.ovl

clean:
	$(STAGE_TIMER) global clean -- sh -c 'rm -rf build/host build/generated build/switch build/packages build/device-lab && if [ "$(CLEAN_EDEN)" = "1" ]; then rm -rf build/eden-test; fi && $(MAKE) -C companion/nro clean || true && rm -rf companion/nro/build-eden companion/nro/pctc-eden.elf companion/nro/pctc-eden.nro companion/nro/pctc-eden.nacp && $(MAKE) -C companion/overlay clean || true && $(MAKE) -C sysmodule clean || true && $(MAKE) -C device_lab/nro clean || true && $(MAKE) -C device_lab/overlay clean || true'
