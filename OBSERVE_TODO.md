# Observe MVP TODO

## Goal

Observe MVP 目标是推进到“真机 observe 测试前的安全准备状态”：

- host 环境可以验证 v1 request queue。
- host 环境可以验证 `offline_code` 和 `status` 的 observe dry-run 处理。
- 可以生成符合新协议的 SD 卡目录布局和 observe 默认配置。
- 真机测试文档具备从开发环境、模拟器到真机 observe 的详细步骤。

## Scope

包含：

- 详细测试方案文档。
- host-side request queue 工具。
- host-side observe processor。
- 安全 SDMC 目录初始化工具。
- observe 队列测试。

暂不包含：

- 可运行的 Switch sysmodule 二进制。
- 可运行的 companion NRO 二进制。
- 真实 PCTL IPC 读取。
- PCTL 写入。
- raw block / suspend。

## Acceptance Criteria

- `python tests/observe/test_observe_queue.py` 通过。
- observe processor 对有效 `offline_code` 写出 `dry_run: true` result。
- observe processor 对 `status` 写出 `dry_run: true` result。
- observe processor 不写 PCTL、不消费 nonce。
- request 从 `pending` 移动到 `processing`，完成后归档到 `done`。
- `tools/protocol_probe.py init` 能生成 observe 默认 SDMC 布局。
- `docs/DETAILED_TEST_PLAN.md` 覆盖开发环境、模拟器和真机 observe 步骤。

## Tasks

- [x] 定义 Observe MVP 范围和验收标准。
- [x] 生成详细测试方案文档。
- [x] 实现 request queue 读写工具。
- [x] 实现 host-side observe processor。
- [x] 实现 observe 安全 SDMC 初始化工具。
- [x] 实现 observe request queue 测试。
- [x] 运行 observe 测试。
- [x] 更新本 TODO，标记已完成任务。

## Verification

- Passed: `python .\tests\mvp\test_token_v1.py`
- Passed: `python .\tests\observe\test_observe_queue.py`
- Passed: `python .\tools\protocol_probe.py init --root .\.tmp\sdmc-observe --device test-device --secret test-secret`

## Current Observe Readiness

- Host-side observe semantics are implemented and tested.
- Safe observe SDMC layout generation is implemented.
- Real Switch observe binary work remains: sysmodule boot2 skeleton, companion NRO request writer, and PCTL read-only adapter.
