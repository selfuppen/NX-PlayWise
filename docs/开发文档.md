# 开发文档

本仓库保存 Nintendo Switch 游玩时间控制项目“任你玩（PlayWise）”的实现文档。品牌主张为“聪明地玩、有节制地玩”，强调自律、自由与不说教；Slogan 为 “Play Wise. Play More.”。当前项目按“稳定性和可测试性优先”推进：先在主机侧证明协议、令牌、队列、控制策略和失败路径，再进入真实 Switch PCTL 写入。

源架构评审稿：

- `docs/稳定性与可测试性优先技术架构计划.md`

建议按以下顺序阅读：

1. `docs/开发指南.md`：编码边界、模块职责和默认工程规则。
2. `docs/架构.md`：运行层次、依赖方向和平台隔离。
3. `docs/协议.md`：SD 卡布局、令牌协议、请求队列、结果 JSON 和错误契约。
4. `docs/测试指南.md`：唯一测试入口，包含本地测试、容器编译打包、真机快速测试和恢复。
5. `docs/实施路线图.md`：实现顺序、当前状态和验收门槛。

## 当前实现重点

- Python v1 token 工具和 observe request queue 已可用；v2 8 位数字短码、CLI 递增 nonce 生成器和可离线安装的家长端静态 PWA 已可用。
- C common core 已包含 token v1/v2、错误映射、时间计算、规则基础、控制策略、result builder 和 result schema validator。
- 平台抽象已包含 `StorageVTable`、`PctlVTable`、time provider、logger，以及 host doubles：`mem_storage`、`pctl_stub`、fake time。
- Switch 平台层已有 `platform/switch/fs_storage.c`、`time_provider.c` 和保守 PCTL adapter；真实 PCTL adapter 支持安全读状态/备份、play timer 写入与效果探针，以及只读的 suspend 探针（`GetPlayTimerEventToRequestSuspension`）。
- `sysmodule/sysmodule_core.c` 已提供 host-testable 队列编排、stuck processing 恢复、backup gate、grant nonce ledger、规则/状态/能力请求、raw block A/B 探针和 result 写入；`sysmodule/` 已提供可在 devkitPro 容器构建的 boot2 sysmodule skeleton。
- `companion/request_client.c` 和 `companion/file_protocol.c` 已提供完整 v1 request JSON 构建、pending 写入、result schema 校验和 request_id 匹配；`companion/nro/` 已提供最小孩子主界面 NRO 骨架。
- `companion/overlay/` 已提供 Tesla `pctc.ovl`：到时后使用 10 键数字面板输入 v2 离线码，共用现有 request/result 队列，不访问 PCTL 或 secret；能否覆盖官方硬限制弹窗仍必须真机验收。
- `python tools/test.py` 是本地 Python 测试唯一入口；`python tools/package_remote.py` 通过 `127.0.0.1:1888` 进入本地 devkitPro 容器，清理并重新生成当前挂载代码的全部 package，然后直接校验 `build/packages/` 中的 zip。
- Companion 家长区提供一次快速真机测试，以及 `disable.flag` 紧急停用和恢复启用入口。

## 核心原则

- 默认 `observe`。
- 优先 fail-open。
- 默认 package 不包含 `boot2.flag`。
- `disable.flag` 覆盖所有控制模式。
- 任何 PCTL 写入前必须备份。
- 只有成功写入并持久化结果后才消费 nonce。
- `common` 逻辑必须可在 host 环境测试。
- play timer 业务写入必须同时通过 raw write 与 runtime effect 探针；raw block 和 suspend 必须由各自真机 probe 验证后才能放行。
