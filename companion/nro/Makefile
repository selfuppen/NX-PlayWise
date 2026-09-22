ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set. Source devkitPro environment before building the Companion NRO)
endif

ifeq ($(strip $(DEVKITA64)),)
$(error DEVKITA64 is not set. Source devkitPro environment before building the Companion NRO)
endif

TARGET ?= pctc
BUILD ?= build
MANIFEST_INCLUDE ?= ../../build/generated
MANIFEST_HEADER := $(MANIFEST_INCLUDE)/release_manifest.h
DEFINES ?=
EDEN_BUILD ?= 0
include ../../common/version.mk
APP_TITLE ?= 任我玩
APP_AUTHOR ?= selfuppen@qq.com
APP_VERSION ?= $(PLAYWISE_VERSION)
APP_ICON := $(CURDIR)/icon.png

CC := $(DEVKITA64)/bin/aarch64-none-elf-gcc
ELF2NRO := $(DEVKITPRO)/tools/bin/elf2nro
PKG_CONFIG := $(DEVKITPRO)/portlibs/switch/bin/aarch64-none-elf-pkg-config
FREETYPE_CFLAGS := $(shell $(PKG_CONFIG) --cflags freetype2)
FREETYPE_LIBS := $(shell $(PKG_CONFIG) --static --libs freetype2)

ARCH := -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -std=gnu99 -O2 -g -Wall -Wextra -Werror -ffunction-sections -MMD -MP $(ARCH)
CFLAGS += -D__SWITCH__ -DPTC_IPC_CLIENT_NRO $(DEFINES) -I../.. -I$(MANIFEST_INCLUDE) -I$(DEVKITPRO)/libnx/include $(FREETYPE_CFLAGS)
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(BUILD)/$(TARGET).map
LIBS := -L$(DEVKITPRO)/libnx/lib $(FREETYPE_LIBS) -lnx

OBJS := \
	$(BUILD)/main.o \
	$(BUILD)/nro_runtime.o \
	$(BUILD)/nro_input_dialogs.o \
	$(BUILD)/nro_preferences.o \
	$(BUILD)/nro_requests.o \
	$(BUILD)/nro_redemption.o \
	$(BUILD)/nro_result_poll.o \
	$(BUILD)/nro_setup.o \
	$(BUILD)/nro_security.o \
	$(BUILD)/nro_pairing.o \
	$(BUILD)/nro_history.o \
	$(BUILD)/nro_policy_requests.o \
	$(BUILD)/nro_support.o \
	$(BUILD)/nro_actions.o \
	$(BUILD)/nro_action_dispatch.o \
	$(BUILD)/nro_plan_actions.o \
	$(BUILD)/nro_input.o \
	$(BUILD)/nro_touch_input.o \
	$(BUILD)/hot_reload.o \
	$(BUILD)/hot_reload_guard.o \
	$(BUILD)/album_restriction.o \
	$(BUILD)/ui_graphics.o \
	$(BUILD)/ui_render_primitives.o \
	$(BUILD)/ui_render_text.o \
	$(BUILD)/ui_render_core.o \
	$(BUILD)/ui_render_components.o \
	$(BUILD)/ui_render_action_card.o \
	$(BUILD)/ui_render_child.o \
	$(BUILD)/ui_render_setup.o \
	$(BUILD)/ui_render_plan.o \
	$(BUILD)/ui_render_pages.o \
	$(BUILD)/ui_render_dialogs.o \
	$(BUILD)/ui_render_dialog_policy.o \
	$(BUILD)/ui_render_dialog_input.o \
	$(BUILD)/ui_render_overlay_account.o \
	$(BUILD)/ui_render_overlay_plan.o \
	$(BUILD)/ui_render_overlay_support.o \
	$(BUILD)/ui_render_overlays.o \
	$(BUILD)/ui_theme.o \
	$(BUILD)/ui_state.o \
	$(BUILD)/ui_plan_state.o \
	$(BUILD)/ui_navigation_state.o \
	$(BUILD)/ui_status_state.o \
	$(BUILD)/ui_input_state.o \
	$(BUILD)/ui_result_state.o \
	$(BUILD)/ui_layout.o \
	$(BUILD)/ui_hit_test.o \
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
	$(BUILD)/activity_history.o \
	$(BUILD)/redemption_history.o \
	$(BUILD)/daily_summary.o \
	$(BUILD)/error_code.o \
	$(BUILD)/request_schema.o \
	$(BUILD)/cJSON.o \
	$(BUILD)/install_defaults.o \
	$(BUILD)/fs_storage.o

DEPENDS := $(OBJS:.o=.d)

# Emulator-only build: run the control core in-process against the simulated
# PCTL adapter, and drop the pctc:u client the emulator cannot reach.
ifeq ($(EDEN_BUILD),1)
OBJS := $(filter-out $(BUILD)/switch_ipc_client.o $(BUILD)/hot_reload.o $(BUILD)/hot_reload_guard.o,$(OBJS))
OBJS += \
	$(BUILD)/eden_runtime.o \
	$(BUILD)/pctl_stub.o \
	$(BUILD)/sysmodule_storage.o \
	$(BUILD)/sysmodule_audit.o \
	$(BUILD)/sysmodule_history.o \
	$(BUILD)/sysmodule_results.o \
	$(BUILD)/sysmodule_control.o \
	$(BUILD)/sysmodule_setup.o \
	$(BUILD)/sysmodule_requests.o \
	$(BUILD)/sysmodule_request_grants.o \
	$(BUILD)/sysmodule_request_recovery.o \
	$(BUILD)/sysmodule_core.o \
	$(BUILD)/play_timer_settings_layout.o \
	$(BUILD)/token_v1.o
endif

.PHONY: all clean

NACP := $(TARGET).nacp

all: $(TARGET).nro

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/main.o: main.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/nro_runtime.o: nro_runtime.c nro_app_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/nro_input_dialogs.o: nro_input_dialogs.c nro_app_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/nro_preferences.o: nro_preferences.c nro_app_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/nro_requests.o: nro_requests.c nro_app_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/nro_redemption.o: nro_redemption.c nro_app_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/nro_result_poll.o: nro_result_poll.c nro_app_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/nro_setup.o: nro_setup.c nro_app_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/nro_security.o: nro_security.c nro_app_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/nro_pairing.o: nro_pairing.c nro_app_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/nro_history.o: nro_history.c nro_app_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/nro_policy_requests.o: nro_policy_requests.c nro_app_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/nro_support.o: nro_support.c nro_app_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/nro_actions.o: nro_actions.c nro_app_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/nro_action_dispatch.o: nro_action_dispatch.c nro_app_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/nro_plan_actions.o: nro_plan_actions.c nro_app_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/nro_input.o: nro_input.c nro_app_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/nro_touch_input.o: nro_touch_input.c nro_app_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/hot_reload.o: hot_reload.c hot_reload.h ../hot_reload_guard.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/hot_reload_guard.o: ../hot_reload_guard.c ../hot_reload_guard.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/album_restriction.o: ../album_restriction.c ../album_restriction.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_graphics.o: ui_graphics.c ui_graphics.h ui_model.h ui_state.h ui_layout.h ui_theme.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_render_primitives.o: ui_render_primitives.c ui_render_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_render_text.o: ui_render_text.c ui_render_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_render_core.o: ui_render_core.c ui_render_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_render_components.o: ui_render_components.c ui_render_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_render_action_card.o: ui_render_action_card.c ui_render_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_render_child.o: ui_render_child.c ui_render_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_render_setup.o: ui_render_setup.c ui_render_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_render_plan.o: ui_render_plan.c ui_render_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_render_pages.o: ui_render_pages.c ui_render_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_render_dialogs.o: ui_render_dialogs.c ui_render_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_render_dialog_policy.o: ui_render_dialog_policy.c ui_render_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_render_dialog_input.o: ui_render_dialog_input.c ui_render_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_render_overlay_account.o: ui_render_overlay_account.c ui_render_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_render_overlay_plan.o: ui_render_overlay_plan.c ui_render_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_render_overlay_support.o: ui_render_overlay_support.c ui_render_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_render_overlays.o: ui_render_overlays.c ui_render_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_theme.o: ui_theme.c ui_theme.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_state.o: ui_state.c ui_state.h ui_model.h ui_layout.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_plan_state.o: ui_plan_state.c ui_state.h ui_model.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_navigation_state.o: ui_navigation_state.c ui_state.h ui_model.h ui_layout.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_status_state.o: ui_status_state.c ui_state.h ui_model.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_input_state.o: ui_input_state.c ui_input_state.h ui_model.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_result_state.o: ui_result_state.c ui_state.h ui_model.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_layout.o: ui_layout.c ui_layout.h ui_state.h ui_model.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_hit_test.o: ui_hit_test.c ui_layout.h ui_state.h ui_model.h | $(BUILD)
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

$(BUILD)/redemption_history.o: ../../common/protocol/redemption_history.c ../../common/protocol/redemption_history.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/activity_history.o: ../../common/protocol/activity_history.c ../../common/protocol/activity_history.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/daily_summary.o: ../../common/usage/daily_summary.c ../../common/usage/daily_summary.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/error_code.o: ../../common/protocol/error_code.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/request_schema.o: ../../common/protocol/request_schema.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/cJSON.o: ../../third_party/cjson/cJSON.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/install_defaults.o: ../../platform/install_defaults.c ../../platform/install_defaults.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/fs_storage.o: ../../platform/switch/fs_storage.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/eden_runtime.o: eden_runtime.c eden_runtime.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/pctl_stub.o: ../../platform/host/pctl_stub.c ../../platform/host/pctl_stub.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sysmodule_core.o: ../../sysmodule/sysmodule_core.c ../../sysmodule/sysmodule_core.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sysmodule_storage.o: ../../sysmodule/sysmodule_storage.c ../../sysmodule/sysmodule_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sysmodule_audit.o: ../../sysmodule/sysmodule_audit.c ../../sysmodule/sysmodule_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sysmodule_history.o: ../../sysmodule/sysmodule_history.c ../../sysmodule/sysmodule_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sysmodule_results.o: ../../sysmodule/sysmodule_results.c ../../sysmodule/sysmodule_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sysmodule_control.o: ../../sysmodule/sysmodule_control.c ../../sysmodule/sysmodule_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sysmodule_setup.o: ../../sysmodule/sysmodule_setup.c ../../sysmodule/sysmodule_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sysmodule_requests.o: ../../sysmodule/sysmodule_requests.c ../../sysmodule/sysmodule_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sysmodule_request_grants.o: ../../sysmodule/sysmodule_request_grants.c ../../sysmodule/sysmodule_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sysmodule_request_recovery.o: ../../sysmodule/sysmodule_request_recovery.c ../../sysmodule/sysmodule_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/play_timer_settings_layout.o: ../../platform/switch/play_timer_settings_layout.c ../../platform/switch/play_timer_settings_layout.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/token_v1.o: ../../common/token/token_v1.c ../../common/token/token_v1.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# Header dependencies are emitted beside each object. Rebuild all objects when
# build flags or the project version included by this Makefile change.
$(OBJS): Makefile ../../common/version.mk $(MANIFEST_HEADER)

-include $(DEPENDS)

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

$(NACP): $(MAKEFILE_LIST)
	$(DEVKITPRO)/tools/bin/nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

$(TARGET).nro: $(TARGET).elf $(NACP) $(APP_ICON)
	$(ELF2NRO) $< $@ --icon=$(APP_ICON) --nacp=$(NACP)

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).nro $(NACP)
