HOST_CC ?= gcc
HOST_CFLAGS ?= -std=c99 -Wall -Wextra -Werror -I.
HOST_BUILD_DIR := build/host
HOST_TEST := $(HOST_BUILD_DIR)/test_host_core

COMMON_SRCS := \
	common/protocol/error_code.c \
	common/protocol/request_schema.c \
	common/protocol/result_builder.c \
	common/token/token_v1.c \
	common/time/ptc_time.c \
	common/rules/rules.c \
	common/policy/control_policy.c

PLATFORM_HOST_SRCS := \
	platform/host/mem_storage.c \
	platform/host/pctl_stub.c \
	platform/host/fake_time.c

ORCH_SRCS := \
	sysmodule/sysmodule_core.c \
	companion/file_protocol.c \
	companion/request_client.c

TEST_SRCS := tests/c/test_host_core.c

.PHONY: all test-host test-python test companion-nro package-safe-nro clean package-safe package-observe package-disabled package-grant package-enforce

all: test

$(HOST_BUILD_DIR):
	mkdir -p $(HOST_BUILD_DIR)

$(HOST_TEST): $(COMMON_SRCS) $(PLATFORM_HOST_SRCS) $(ORCH_SRCS) $(TEST_SRCS) | $(HOST_BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $(COMMON_SRCS) $(PLATFORM_HOST_SRCS) $(ORCH_SRCS) $(TEST_SRCS)

test-host: $(HOST_TEST)
	$(HOST_TEST)

test-python:
	python3 tests/mvp/test_token_v1.py
	python3 tests/observe/test_observe_queue.py

test: test-host test-python

companion-nro:
	$(MAKE) -C companion/nro
	mkdir -p build/switch
	cp companion/nro/pctc.nro build/switch/pctc.nro

package-safe-nro: companion-nro
	python3 tools/package_sdmc.py --mode safe --out build/packages/safe-nro --zip build/packages/safe-nro.zip --nro build/switch/pctc.nro

package-safe package-observe package-disabled package-grant package-enforce:
	python3 tools/package_sdmc.py --mode $(subst package-,,$@) --out build/packages/$(subst package-,,$@)

clean:
	rm -rf build
