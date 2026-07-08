# MVP TODO

## MVP Definition

MVP 目标是先交付一个可在 host 环境验证的最小核心闭环，不依赖 Switch 真机、不依赖 libnx、不触碰 PCTL。

MVP 必须做到：

- 固定 v1 协议常量、错误码和请求类型。
- 提供 20 字符 Crockford Base32 离线授权码生成与校验工具。
- 使用 payload v1：version、action、minutes、day index、nonce。
- 使用 HMAC-SHA256 截断 40 bit。
- 提供 deterministic fixture，避免手写测试码。
- 提供 host-side 测试脚本，验证有效码、错密钥、错日期、超上限和重复 nonce 的基础行为。
- 为后续 C common core、sysmodule 和 companion 实现留下稳定目录结构。

MVP 暂不包含：

- Switch sysmodule。
- companion NRO。
- 真实 PCTL adapter。
- SDMC request queue 运行时。
- raw block / suspend 真机 probe。
- UI。

## Acceptance Criteria

- `python tools/grant_code.py --minutes 30 --device test-device --secret test-secret --day-index 2380 --nonce 4660` 可以输出稳定授权码。
- `python tests/mvp/test_token_v1.py` 通过。
- 错误路径返回稳定 reason：`bad_signature`、`wrong_date`、`minutes_exceed_limit`、`used_token`。
- 项目目录至少包含 `common/`、`tools/`、`tests/mvp/`。

## Tasks

- [x] 定义 MVP 范围和验收标准。
- [x] 建立 MVP 目录结构。
- [x] 定义 C 侧协议常量与错误码头文件。
- [x] 实现 Python v1 授权码生成/校验模块。
- [x] 实现 CLI：`tools/grant_code.py`。
- [x] 实现 deterministic fixture。
- [x] 实现 MVP token 测试。
- [x] 运行 MVP 测试并修复问题。
- [x] 更新本 TODO，标记已完成任务。

## Verification

- Passed: `python .\tests\mvp\test_token_v1.py`

