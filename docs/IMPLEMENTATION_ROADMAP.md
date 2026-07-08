# 实施路线图

本文档把架构评审稿拆成可执行阶段，并记录当前已实现和未实现的功能状态。

## 当前状态

状态说明：

- 已完成：已经实现，并且有 host-side 验证覆盖。
- 部分完成：已有一部分 host-side 能力，但阶段尚未完整。
- 未开始：还没有生产实现。
- 受阻：需要 devkitPro/libnx/Switch 真机环境才能构建或验证。

| 功能域 | 状态 | 已实现 | 未实现 / 剩余工作 |
| --- | --- | --- | --- |
| 架构与计划文档 | 已完成 | 稳定性优先架构评审、开发指南、协议文档、详细测试方案、真机测试清单、MVP TODO 和 Observe TODO。 | 后续实现决策变化时继续同步文档。 |
| Token v1 协议 | 已完成 | `tools/ptc_token_v1.py` 实现 20 字符 Crockford Base32 授权码生成与校验；`tools/grant_code.py` 提供 deterministic CLI；`tools/make_fixtures.py` 生成 fixture。 | sysmodule/companion 仍需要原生 C token codec。 |
| 协议常量 | 部分完成 | 已有 schema version、request type、token payload 常量和稳定错误码 C 头文件。 | C 侧 error reason/message 映射还未实现。 |
| Host MVP 测试 | 已完成 | `tests/mvp/test_token_v1.py` 覆盖有效码、CLI 稳定输出、错密钥、错日期、重复 nonce 和超上限拒绝。 | 还没有编译型 C 单测 runner。 |
| Host observe 队列 | 已完成 | `tools/ptc_request_queue.py`、`tools/ptc_observe_processor.py`、`tools/protocol_probe.py` 支持 SDMC 布局初始化、pending request 写入、observe dry-run 处理、result 写入和 request 归档。 | 这只是 host-side probe，不是 Switch sysmodule runtime。 |
| Observe 测试 | 已完成 | `tests/observe/test_observe_queue.py` 覆盖 `status`、有效 `offline_code`、坏签名、超上限、dry-run result、done 归档和 observe 不消费 nonce。 | 还没有执行模拟器或真机 observe 测试。 |
| SDMC observe 布局 | 已完成 | `protocol_probe.py init` 可生成兼容 `sdmc:/switch/play-time-control/` 的安全布局，默认 `control_mode: observe`。 | 还没有 SD 卡拷贝/打包脚本。 |
| Request queue runtime | 部分完成 | host-side pending -> processing -> done -> results 状态迁移已实现。 | C 侧 sysmodule queue scanner、启动时恢复 stuck processing request、storage 失败处理还未实现。 |
| Config/rules/state/capabilities | 部分完成 | host-side `protocol_probe.py init` 会写入 v1 JSON 默认文件。 | C 侧 parser/validator、运行态 store、持久化更新还未实现。 |
| Control policy | 部分完成 | host processor 已体现 observe 语义：dry-run、不写 PCTL、不消费 nonce。 | 完整 `disabled`/`grant`/`enforce` 策略、unlimited guard、backup gate、高风险 capability gate 仍需 C 实现和测试。 |
| Nonce ledger | 部分完成 | Python token verifier 支持传入 used nonce set；observe 测试验证不会创建 nonce ledger。 | 持久化 ledger 查询/追加、grant 模式成功写入后消费 nonce 尚未实现。 |
| Storage 抽象 | 未开始 | 无 C 实现。 | `StorageVTable`、SDMC adapter、`mem_storage`、atomic write、append、rename、list、故障注入。 |
| PCTL 抽象 | 未开始 | 无实现。 | `PctlVTable`、`pctl_stub`、真实 libnx adapter、read status、apply target、start/stop timer、raw block probe、suspend probe。 |
| Sysmodule | 未开始 / 受阻 | 无实现。 | boot lifecycle、service init、request dispatcher、event logger、backup writer、SDMC adapter、observe boot2 binary。需要 devkitPro/libnx 构建。 |
| Companion NRO | 未开始 / 受阻 | 无实现。 | 孩子主界面、离线码输入、request client、result 展示、timeout handling、家长区。需要 devkitPro/libnx 构建。 |
| 模拟器测试 | 未开始 / 受阻 | 已有详细步骤文档。 | 需要 companion NRO 后才能跑模拟器验证。 |
| 真机 observe | 未开始 / 受阻 | 已有详细步骤文档；可以生成安全 SDMC observe 布局。 | 需要 sysmodule boot2 skeleton、companion 或 request-writer NRO、PCTL read-only adapter，并需要真机验证。 |
| Grant/enforce 阶段 | 未开始 / 受阻 | 已有架构和测试方案。 | PCTL 写入、backup、成功写入后消费 nonce、enforce timer refresh、raw block/suspend probe 和 gating。 |

## 当前验证命令

当前 host-side 验证：

```powershell
python .\tests\mvp\test_token_v1.py
python .\tests\observe\test_observe_queue.py
python .\tools\protocol_probe.py init --root .\.tmp\sdmc-observe --device test-device --secret test-secret
```

## 阶段 1：Common Core 基础

状态：部分完成。

构建内容：

| 项目 | 状态 | 说明 |
| --- | --- | --- |
| 稳定错误码 | 已完成 | `common/protocol/error_code.h` 已定义 enum。 |
| 错误 reason/message 映射 | 未开始 | C 侧结构化 result 需要。 |
| 时间计算 | 未开始 | 仍需 C 侧 day index、weekday、bedtime 逻辑。 |
| Token v1 Python 实现 | 已完成 | 已用于 CLI、fixtures 和 tests。 |
| Token v1 C 实现 | 未开始 | sysmodule/companion 原生验码前必须实现。 |
| Request/result schema 常量 | 部分完成 | 已有 request type 和 schema version 头文件；result builder 缺失。 |
| deterministic fixture 生成器 | 已完成 | `tools/make_fixtures.py`。 |

验收标准：

- 单元测试覆盖所有纯逻辑函数。
- fixture 生成可复现。
- common 代码不依赖 libnx 或文件系统。

## 阶段 2：测试框架与接口抽象

状态：未开始。

构建内容：

- `StorageVTable`。
- `PctlVTable`。
- `mem_storage`。
- `pctl_stub`。
- fake time provider。
- 基础 host integration test runner。

验收标准：

- integration tests 可以不依赖 Switch 硬件运行。
- storage 失败和 PCTL 失败都可以注入。

## 阶段 3：策略与队列核心

状态：部分完成。

构建内容：

| 项目 | 状态 | 说明 |
| --- | --- | --- |
| Rule engine | 未开始 | weekly template、today override、bedtime、parent unlock 需要。 |
| Control policy | 部分完成 | Python 中已有 observe dry-run 行为。 |
| Nonce ledger 行为 | 部分完成 | Python verifier 可检查传入的 used set；持久化 ledger 缺失。 |
| Request queue 状态机 | host probe 已完成 | `ptc_request_queue.py` 实现 host pending -> processing -> done。 |
| Result builder | 部分完成 | Python observe result builder 已有；C result builder 缺失。 |

验收标准：

- pending -> processing -> done -> result 可在 host tests 中跑通。
- observe 永远不写入、不消费 nonce。
- grant 只有成功写入后才消费 nonce。
- capability gates 默认拒绝 raw block 和 suspend。

## 阶段 4：Sysmodule Skeleton

状态：未开始 / 受 Switch 构建环境阻塞。

构建内容：

- boot lifecycle。
- config/rules/state/capabilities 加载。
- SDMC storage adapter。
- event logger。
- backup writer。
- request dispatcher。

验收标准：

- `disabled` 模式可以 boot、写日志并返回 result。
- 坏请求返回结构化错误。
- stuck processing request 可以恢复。

## 阶段 5：PCTL Adapter

状态：未开始 / 受 Switch 构建环境和真机验证阻塞。

构建内容：

- 真实 PCTL read status。
- 真实 apply target。
- start/stop timer wrappers。
- raw block probe path。
- suspend probe path。

验收标准：

- adapter 与业务逻辑隔离。
- host tests 仍使用 `pctl_stub`。
- 真实写入仍受 control policy 和 backup 保护。

## 阶段 6：Companion 基础

状态：未开始 / 受 Switch 构建环境阻塞。

构建内容：

- 孩子状态主界面。
- 离线码输入。
- request client。
- matching-result wait。
- timeout display。

验收标准：

- 模拟器可以写 request。
- 手动插入的 result 可以正确展示。
- timeout 显示为后台未响应，而不是业务失败。

## 阶段 7：家长区

状态：未开始。

构建内容：

- PIN 初始化。
- PIN hash 校验。
- 今日额度操作。
- weekly template 编辑。
- bedtime 编辑。
- parent unlock 控制。
- raw block 和 suspend probe 控制。

验收标准：

- 孩子 UI 不能直接访问家长操作。
- 家长区 request 符合协议。
- 高风险操作由 sysmodule result 明确 gate。

## 阶段 8：打包与真机阶段

状态：部分完成 / 受 Switch 二进制阻塞。

构建内容：

| 包 / 阶段 | 状态 | 说明 |
| --- | --- | --- |
| 安全 SDMC observe 布局 | 已完成 | `protocol_probe.py init` 会创建 app 目录和默认 JSON 文件。 |
| Safe NRO-only package | 未开始 | 需要 companion NRO。 |
| Disabled boot2 package | 未开始 | 需要 sysmodule binary。 |
| Observe package | 未开始 | 需要 sysmodule、companion/request writer 和 PCTL read-only adapter。 |
| Grant package | 未开始 | 需要 backup 和 write path。 |
| Enforce package | 未开始 | 需要 grant 稳定性和强控制验证。 |

验收标准：

- package 默认使用安全配置。
- `boot2.flag` 只有显式请求时才包含。
- 遵循 `REAL_SWITCH_TESTING.md` 的真机检查清单。

## 后续优化

在 v1 行为稳定后再做：

- UI polish。
- 报告展示。
- 敏感字段遮罩。
- 历史 request/result 清理策略。
- 桌面端协议 probe 增强。
- 更多离线 token action。
