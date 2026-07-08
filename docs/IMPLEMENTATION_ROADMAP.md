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
| Token v1 协议 | 已完成 | `tools/ptc_token_v1.py` 实现 20 字符 Crockford Base32 授权码生成与校验；`tools/grant_code.py` 提供 deterministic CLI；`tools/make_fixtures.py` 生成 fixture；`common/token/token_v1.c` 提供 C 侧原生 codec 并与 fixture 对齐。 | 后续只需在真实 sysmodule/companion 中接入该 codec。 |
| 协议常量 | 部分完成 | 已有 schema version、request type、token payload 常量、稳定错误码 C 头文件，以及 `common/protocol/error_code.c` 的 reason/message 映射。 | 更完整的 request/result schema validator 仍需补充。 |
| Host MVP 测试 | 已完成 | `tests/mvp/test_token_v1.py` 覆盖有效码、CLI 稳定输出、错密钥、错日期、重复 nonce 和超上限拒绝；`tests/c/test_host_core.c` 覆盖 C token fixture parity。 | 可继续扩大 C 单测覆盖，但基础 runner 已存在。 |
| Host observe 队列 | 已完成 | `tools/ptc_request_queue.py`、`tools/ptc_observe_processor.py`、`tools/protocol_probe.py` 支持 SDMC 布局初始化、pending request 写入、observe dry-run 处理、result 写入和 request 归档。 | 这只是 host-side probe，不是 Switch sysmodule runtime。 |
| Observe 测试 | 已完成 | `tests/observe/test_observe_queue.py` 覆盖 `status`、有效 `offline_code`、坏签名、超上限、dry-run result、done 归档和 observe 不消费 nonce。 | 还没有执行模拟器或真机 observe 测试。 |
| SDMC observe 布局 | 已完成 | `protocol_probe.py init` 可生成兼容 `sdmc:/switch/play-time-control/` 的安全布局，默认 `control_mode: observe`；`tools/package_sdmc.py` 可生成 staged SDMC package。 | 后续 package 需要接入真实 sysmodule/NRO 二进制。 |
| Request queue runtime | 部分完成 | Python host probe 已实现 pending -> processing -> done -> results；`sysmodule/sysmodule_core.c` 已实现 C host queue scanner 和 stuck processing 恢复。 | 真实 SDMC adapter、event logging 细节和更多 request 类型仍需补充。 |
| Config/rules/state/capabilities | 部分完成 | host-side `protocol_probe.py init` 会写入 v1 JSON 默认文件；C host core 可加载 config/capabilities，并有规则基础结构。 | 完整 C JSON parser/validator、rules/state 持久化更新仍需补充。 |
| Control policy | 部分完成 | Python observe 语义已实现；`common/policy/control_policy.c` 已实现 `disabled`/`observe`/`grant` 基础策略、unlimited guard、backup gate、raw/suspend capability gate。 | parent 操作、完整 enforce 行为和真实 PCTL 联动仍需补充。 |
| Nonce ledger | 部分完成 | Python verifier 支持 used nonce set；C sysmodule host core 已实现 `ledger/used_nonces.jsonl` 查询/追加，并在 grant 成功写入 result 后消费 nonce。 | 需要更多故障注入用例和真实 SDMC 持久化验证。 |
| Storage 抽象 | 部分完成 | 已实现 `StorageVTable` 和 host `mem_storage`，支持 read、atomic write、append、rename、remove、exists、list JSON 和故障注入。 | 真实 SDMC adapter 仍未实现。 |
| PCTL 抽象 | 部分完成 | 已实现 `PctlVTable` 和 host `pctl_stub`，支持 read status、backup、apply target、start/stop timer、raw block probe、suspend probe。 | 真实 libnx adapter 仍未实现。 |
| Sysmodule | 部分完成 / 受阻 | 已有 host-testable `sysmodule_core`：queue scanner、stuck recovery、status/offline_code、backup gate、nonce ledger、result 写入。 | boot lifecycle、service init、event logger、SDMC adapter、observe boot2 binary 仍需 devkitPro/libnx 构建。 |
| Companion NRO | 部分完成 / 受阻 | 已有 `companion/request_client.c` 可生成 status、offline_code 和家长分钟类 request JSON。 | 孩子主界面、离线码输入 UI、result 展示、timeout handling、家长区仍需 devkitPro/libnx 构建。 |
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

远程 devkitPro 容器验证：

```sh
ssh 249-nintendo-switch-dev 'cd /ws/switch-play-time-control-local && git pull --ff-only origin master && make'
```

Package 验证：

```sh
make package-safe package-observe
python3 tools/package_sdmc.py --mode observe --boot2 --out build/packages/observe-boot2
```

## 阶段 1：Common Core 基础

状态：部分完成。

构建内容：

| 项目 | 状态 | 说明 |
| --- | --- | --- |
| 稳定错误码 | 已完成 | `common/protocol/error_code.h` 已定义 enum。 |
| 错误 reason/message 映射 | 已完成 | `common/protocol/error_code.c` 已提供 C 侧 reason 和中文 message。 |
| 时间计算 | 已完成 | `common/time/ptc_time.c` 已实现 day index、weekday、bedtime 跨天判断。 |
| Token v1 Python 实现 | 已完成 | 已用于 CLI、fixtures 和 tests。 |
| Token v1 C 实现 | 已完成 | `common/token/token_v1.c` 已实现 C token codec，并通过 C host test 与 Python fixture 对齐。 |
| Request/result schema 常量 | 部分完成 | 已有 request type 和 schema version 头文件；`common/protocol/result_builder.c` 已可生成基础 ok/error result。 |
| deterministic fixture 生成器 | 已完成 | `tools/make_fixtures.py`。 |

验收标准：

- 单元测试覆盖所有纯逻辑函数。
- fixture 生成可复现。
- common 代码不依赖 libnx 或文件系统。

## 阶段 2：测试框架与接口抽象

状态：部分完成。

构建内容：

- `StorageVTable`：已完成 host 接口。
- `PctlVTable`：已完成 host 接口。
- `mem_storage`：已完成。
- `pctl_stub`：已完成。
- fake time provider：已完成。
- 基础 host integration test runner：已完成，入口为 `make test-host`。

验收标准：

- integration tests 可以不依赖 Switch 硬件运行。
- storage 失败和 PCTL 失败都可以注入。

## 阶段 3：策略与队列核心

状态：部分完成。

构建内容：

| 项目 | 状态 | 说明 |
| --- | --- | --- |
| Rule engine | 部分完成 | C 侧已有 weekly template、today override 和 bedtime 基础结构；parent unlock 与持久化更新仍需补充。 |
| Control policy | 部分完成 | C 侧已有 `disabled`/`observe`/`grant` 基础策略、unlimited guard、backup gate 和 capability gate。 |
| Nonce ledger 行为 | 部分完成 | C host core 已支持 `ledger/used_nonces.jsonl` 查询/追加；更多故障注入仍需补充。 |
| Request queue 状态机 | 部分完成 | Python probe 和 C host sysmodule core 均实现 pending -> processing -> done；真实 SDMC adapter 仍需补充。 |
| Result builder | 部分完成 | Python observe result builder 和 C `result_builder` 均已有；完整 schema 字段仍需扩展。 |

验收标准：

- pending -> processing -> done -> result 可在 host tests 中跑通。
- observe 永远不写入、不消费 nonce。
- grant 只有成功写入后才消费 nonce。
- capability gates 默认拒绝 raw block 和 suspend。

## 阶段 4：Sysmodule Skeleton

状态：部分完成 / 受 Switch 构建环境阻塞。

构建内容：

- boot lifecycle：未开始。
- config/rules/state/capabilities 加载：config/capabilities host 读取已完成，rules/state 完整持久化仍需补充。
- SDMC storage adapter：未开始。
- event logger：未开始。
- backup writer：host backup gate 已完成，真实 SDMC backup writer 仍需补充。
- request dispatcher：host `status` 和 `offline_code` 已完成，更多 request 类型仍需补充。

验收标准：

- `disabled` 模式可以 boot、写日志并返回 result。
- 坏请求返回结构化错误。
- stuck processing request 可以恢复。

## 阶段 5：PCTL Adapter

状态：部分完成 / 受 Switch 构建环境和真机验证阻塞。

构建内容：

- host `PctlVTable` 和 `pctl_stub` 已完成。
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

状态：部分完成 / 受 Switch 构建环境阻塞。

构建内容：

- 孩子状态主界面。
- 离线码输入。
- request client：C request JSON builder 已完成。
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
| Safe SDMC package | 已完成 | `make package-safe` 生成默认 observe 安全布局，不包含 `boot2.flag`。 |
| Observe SDMC package | 已完成 | `make package-observe` 生成 observe 配置；默认不包含 `boot2.flag`。 |
| 显式 boot2 package | 部分完成 | `tools/package_sdmc.py --boot2` 可生成 `boot2.flag`；仍需真实 sysmodule binary。 |
| Safe NRO-only package | 受阻 | 需要 companion NRO 二进制。 |
| Disabled boot2 package | 受阻 | 需要 sysmodule binary。 |
| Grant package | 受阻 | 需要真实 backup 和 PCTL write path。 |
| Enforce package | 受阻 | 需要 grant 稳定性和强控制验证。 |

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
