# 开发文档

本目录保存 Nintendo Switch 游玩时间控制项目的实现文档。当前项目按“稳定性和可测试性优先”推进：先在主机侧证明协议、令牌、队列、控制策略和失败路径，再进入真实 Switch PCTL 写入。

源架构评审稿：

- `TECH_ARCHITECTURE_STABILITY_TESTABILITY_PLAN.md`

建议按以下顺序阅读：

1. `DEVELOPMENT_GUIDE.md`：编码边界、模块职责和默认工程规则。
2. `ARCHITECTURE.md`：运行层次、依赖方向和平台隔离。
3. `PROTOCOL.md`：SD 卡布局、令牌协议、请求队列、结果 JSON 和错误契约。
4. `TESTING.md`：主机侧单元测试、集成测试和远程容器验证方式。
5. `DETAILED_TEST_PLAN.md`：开发、模拟器和真机 observe 的分步测试。
6. `REAL_SWITCH_TESTING.md`：真机分阶段验证清单。
7. `IMPLEMENTATION_ROADMAP.md`：实现顺序、当前状态和验收门槛。

## 当前实现重点

- Python v1 token 工具和 observe request queue 已可用。
- C common core 已包含 token v1、错误映射、时间计算、规则基础、控制策略和 result builder。
- 平台抽象已包含 `StorageVTable`、`PctlVTable`、time provider、logger，以及 host doubles：`mem_storage`、`pctl_stub`、fake time。
- `sysmodule/sysmodule_core.c` 已提供 host-testable 队列编排、stuck processing 恢复、backup gate、grant nonce ledger 和 result 写入。
- `companion/request_client.c` 已提供 request JSON 构建；真实 NRO UI 仍待实现。
- `Makefile` 已提供主机 C 测试、Python 测试和 SDMC package 目标。

## 核心原则

- 默认 `observe`。
- 优先 fail-open。
- 默认 package 不包含 `boot2.flag`。
- `disable.flag` 覆盖所有控制模式。
- 任何 PCTL 写入前必须备份。
- 只有成功写入并持久化结果后才消费 nonce。
- `common` 逻辑必须可在 host 环境测试。
- raw block 和 suspend 必须由真机 probe 验证后才能放行。
