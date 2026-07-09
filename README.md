# 开发文档

本仓库保存 Nintendo Switch 游玩时间控制项目的实现文档。当前项目按“稳定性和可测试性优先”推进：先在主机侧证明协议、令牌、队列、控制策略和失败路径，再进入真实 Switch PCTL 写入。

源架构评审稿：

- `docs/稳定性与可测试性优先技术架构计划.md`

建议按以下顺序阅读：

1. `docs/开发指南.md`：编码边界、模块职责和默认工程规则。
2. `docs/架构.md`：运行层次、依赖方向和平台隔离。
3. `docs/协议.md`：SD 卡布局、令牌协议、请求队列、结果 JSON 和错误契约。
4. `docs/测试指南.md`：主机侧单元测试、集成测试和远程容器验证方式。
5. `docs/详细测试计划.md`：开发、模拟器和真机 observe 的分步测试。
6. `docs/真机测试指南.md`：真机分阶段验证清单。
7. `docs/实施路线图.md`：实现顺序、当前状态和验收门槛。

## 当前实现重点

- Python v1 token 工具和 observe request queue 已可用。
- C common core 已包含 token v1、错误映射、时间计算、规则基础、控制策略、result builder 和 result schema validator。
- 平台抽象已包含 `StorageVTable`、`PctlVTable`、time provider、logger，以及 host doubles：`mem_storage`、`pctl_stub`、fake time。
- Switch 平台层已有 `platform/switch/fs_storage.c`、`time_provider.c` 和保守 PCTL adapter；当前真实 PCTL adapter 支持安全读状态/备份，写入、raw block 和 suspend probe 在真机 raw layout 验证前返回稳定错误。
- `sysmodule/sysmodule_core.c` 已提供 host-testable 队列编排、stuck processing 恢复、backup gate、grant nonce ledger、规则/状态/能力请求和 result 写入；`sysmodule/` 已提供可远程构建的 boot2 sysmodule skeleton。
- `companion/request_client.c` 和 `companion/file_protocol.c` 已提供完整 v1 request JSON 构建、pending 写入、result schema 校验和 request_id 匹配；`companion/nro/` 已提供最小孩子主界面 NRO 骨架。
- `Makefile` 已提供主机 C 测试、Python 测试、Companion NRO、sysmodule NSP、SDMC package 和 disabled/observe boot2 package 目标。

## 核心原则

- 默认 `observe`。
- 优先 fail-open。
- 默认 package 不包含 `boot2.flag`。
- `disable.flag` 覆盖所有控制模式。
- 任何 PCTL 写入前必须备份。
- 只有成功写入并持久化结果后才消费 nonce。
- `common` 逻辑必须可在 host 环境测试。
- raw block 和 suspend 必须由真机 probe 验证后才能放行。
