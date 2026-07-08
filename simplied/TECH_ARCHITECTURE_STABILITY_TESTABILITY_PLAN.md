# Nintendo Switch 家长控制离线加时稳定性与可测试性优先技术架构 Plan

本文档基于 `TECH_ARCHITECTURE_CLEAN_SLATE.md`，并参考 `PLAY_TIME_CONTROL_22_5.md` 与 `PRODUCT_SPEC_REBUILD.md`，用于定义新项目的稳定性与可测试性优先架构方向。

本方案不覆盖原 clean-slate 文档，也不要求兼容旧数据、旧 16 字符授权码、旧单文件 `grant_request.json` / `grant_result.json` 协议或旧 SD 卡目录。它是一份高层架构评审稿，重点说明边界、风险控制、测试策略和实施顺序。

## 1. 总体结论

v1 按“完整本地管控”设计，但稳定性优先于功能展示丰富度。

首版纳入：

- 离线加时。
- 今日额度管理。
- 周模板。
- bedtime。
- parent unlock。
- raw block / suspend probe。
- 状态查询。
- request queue。
- 日志审计。
- PIN 保护的家长区。
- host-side 单元测试和集成测试。

其中 raw block 与 suspend 首版实现完整执行路径，但默认关闭。只有真机 probe 成功并写入 capability 后，普通请求才允许执行这些高风险能力。

核心架构原则：

- 默认 `observe`，默认不启用 boot2。
- fail-open 优先。
- 写 PCTL 前必须备份。
- observe 不写 PCTL、不消费 nonce。
- 只有 PCTL 写入成功后才消费 nonce。
- Switch 相关风险集中在薄 adapter 层。
- common core 必须可在 desktop/host 环境编译和测试。
- 所有错误路径必须有结构化 result，不能只让 companion 超时。

## 2. 架构边界

新架构采用四层边界。

### 2.1 common core

`common core` 是纯 C 逻辑层，不依赖 libnx，不直接读写文件，不知道 SD 卡路径。

职责：

- token 编解码。
- HMAC 签名校验。
- day index、weekday、bedtime 跨天计算。
- request/result schema 定义。
- error code 定义和映射。
- rule engine。
- control policy。
- nonce 消费规则。

设计目标：

- 可在 host 环境用普通 C 编译器构建。
- 可被 unit tests 覆盖。
- 不包含真实 PCTL IPC、SDMC、UI、日志文件写入等平台细节。

### 2.2 platform adapters

`platform adapters` 集中隔离 Switch 环境风险。

主要 adapter：

- SDMC storage。
- PCTL IPC。
- time service。
- logging。
- atomic file operation。

adapter 对业务层暴露 C vtable 接口。测试环境使用 `mem_storage` 和 `pctl_stub` 替代真实实现。

建议接口能力：

```c
typedef struct Storage Storage;

typedef struct {
    bool (*read_text)(Storage *, const char *path, char *out, size_t out_size);
    bool (*write_text_atomic)(Storage *, const char *path, const char *text);
    bool (*append_line)(Storage *, const char *path, const char *line);
    bool (*rename)(Storage *, const char *from, const char *to);
    bool (*remove)(Storage *, const char *path);
    bool (*exists)(Storage *, const char *path);
    bool (*list_json)(Storage *, const char *dir, char names[][128], size_t max, size_t *count);
} StorageVTable;
```

```c
typedef struct Pctl Pctl;

typedef struct {
    Result (*read_status)(Pctl *, PctlStatus *out);
    Result (*apply_target)(Pctl *, const PctlTarget *target, PctlBackup *backup);
    Result (*start_timer)(Pctl *);
    Result (*stop_timer)(Pctl *);
    Result (*probe_raw_block)(Pctl *, ProbeResult *out);
    Result (*probe_suspend)(Pctl *, ProbeResult *out);
} PctlVTable;
```

### 2.3 sysmodule orchestration

`sysmodule orchestration` 负责把文件协议、规则、PCTL adapter 和日志串起来。

职责：

- boot2 启动生命周期。
- 服务初始化和延迟重试。
- request queue 扫描。
- stuck processing request 恢复。
- config/rules/state/capabilities 加载。
- nonce ledger 查询和写入。
- PCTL 写入前备份。
- event logging。
- result 写入。
- control mode 执行。

sysmodule 不应该直接实现 token 细节、规则细节或 raw PCTL 布局判断。它只做编排和错误处理。

### 2.4 companion UI

`companion UI` 负责用户交互，不承担最终安全判断。

孩子主界面：

- 今日状态。
- 剩余时间。
- 离线码输入。
- 最近结果。
- 刷新状态。

家长区：

- PIN 初始化和校验。
- 设置今日额度。
- 今日加时。
- 今日不限。
- 今日禁玩。
- 恢复周模板。
- 设置七天模板。
- 设置 bedtime。
- 设置 limit action。
- parent unlock start/end。
- raw block / suspend probe。
- 查看近期结果和日志摘要。

companion 提交请求后等待同名 result。超时只能表示“后台未响应”，不能展示为业务失败。

## 3. 技术选型

实现语言建议纯 C 优先：

- sysmodule：C。
- companion NRO：C。
- common core：纯 C。
- tools：Python。

选择纯 C 的理由：

- Switch sysmodule runtime 风险更低。
- host-side 测试替身更直接。
- vtable 接口足够表达 Storage/PCTL 边界。
- 避免 exceptions、RTTI、复杂静态初始化和隐式动态分配。

如后续确实需要 C++，应限制在 common core 或工具层，并满足：

- `-fno-exceptions`
- `-fno-rtti`
- 不依赖复杂静态初始化。
- 不在 sysmodule 热路径使用不可控动态分配。

## 4. SD 卡布局和文件协议

新 SD 卡目录：

```text
sdmc:/switch/play-time-control/
```

建议布局：

```text
sdmc:/switch/play-time-control/
├── config.json
├── auth.json
├── rules.json
├── state.json
├── capabilities.json
├── inbox/
│   ├── pending/
│   ├── processing/
│   └── done/
├── results/
├── logs/
│   ├── sysmodule.log
│   └── events.jsonl
├── ledger/
│   └── used_nonces.jsonl
├── backups/
│   └── last_pctl_backup.txt
└── flags/
    └── disable.flag
```

所有 JSON 文件从 v1 开始都必须带 `version`。

核心配置文件：

- `config.json`：`device_id`、`grant_secret`、`max_add_minutes`、`control_mode`、`allow_unlimited_to_limited`、`default_request_timeout_ms`。
- `auth.json`：PIN salt + hash，替代明文 `settings.conf`。
- `rules.json`：今日 override、七天模板、bedtime、limit action。
- `state.json`：parent unlock、bedtime 当前状态、最近应用状态。
- `capabilities.json`：`raw_block_verified`、`suspend_verified` 和验证时间。

## 5. 授权码协议

授权码升级为 20 字符 Crockford Base32：

```text
XXXXX-XXXXX-XXXXX-XXXXX
```

容量：

- payload：60 bit。
- MAC：40 bit。
- total：100 bit。

payload v1：

```text
version:              4 bit
action:               4 bit
minutes:             11 bit
day_index_since_2020:16 bit
nonce:               25 bit
```

HMAC 输入：

```text
"PTC1" || device_id || NUL || payload_bits_as_bytes
```

v1 token 只支持：

```c
TOKEN_ACTION_ADD_TODAY_MINUTES = 1
```

不在 v1 短码中支持 set limit、unlock、disable 等更高权限动作，避免离线码权限过大。家长区可以通过 request queue 发起这些本地管理请求。

## 6. Request Queue

不再使用单个 `grant_request.json` / `grant_result.json`。新协议使用 request queue，避免覆盖、旧 result 误读和超时后晚到处理不清的问题。

流程：

1. companion 生成 request id：`<unix_ms>-<random16>`。
2. companion 写入 `inbox/pending/<request_id>.json.tmp`。
3. 写入完成后 rename 为 `inbox/pending/<request_id>.json`。
4. sysmodule 处理时 rename 到 `inbox/processing/<request_id>.json`。
5. 完成后写 `results/<request_id>.json`。
6. 原 request 移动到 `inbox/done/<request_id>.json`。

request v1 基本结构：

```json
{
  "version": 1,
  "request_id": "1783526400123-a4f2",
  "type": "offline_code",
  "created_at": 1783526400,
  "payload": {}
}
```

v1 request 类型：

- `offline_code`
- `status`
- `set_today_limit`
- `add_today_minutes`
- `disable_today_limit`
- `block_today`
- `restore_today_policy`
- `set_weekly_template`
- `set_bedtime`
- `set_limit_action`
- `parent_unlock_start`
- `parent_unlock_end`
- `probe_raw_block`
- `probe_suspend`

result v1 必须包含：

- `version`
- `request_id`
- `type`
- `status`
- `mode`
- `dry_run`
- `state`
- `capabilities`
- `completed_at`

错误 result 必须包含：

- numeric `code`
- stable `reason`
- user-facing Chinese `message`

所有错误路径都必须写 result，除非 storage 本身完全不可写。

## 7. 控制模式和安全策略

控制模式是稳定性设计的核心。

```text
disabled:
  拒绝请求。
  不读 PCTL。
  不写 PCTL。
  可写日志和 result。

observe:
  读取状态。
  验证请求。
  计算预期结果。
  不写 PCTL。
  不消费 nonce。
  result 必须标记 dry_run=true。

grant:
  允许有效授权和家长区操作写 PCTL。
  写入前必须备份。
  写入成功后消费 nonce。

enforce:
  包含 grant 的能力。
  允许开机启用 play timer。
  允许写入后刷新 play timer。
  允许执行 bedtime/suspend 等强控制。
```

`flags/disable.flag` 覆盖所有模式。一旦存在，系统进入 fail-open，不触碰 PCTL。

未知 `control_mode` 必须降级为 `observe`，并写 warning event。

任何 PCTL 写入必须满足：

- control policy 允许。
- 当前请求合法。
- 目标状态已计算完成。
- backup 成功。
- PCTL adapter 返回成功。

nonce 消费规则：

- observe 不消费。
- 无效码不消费。
- PCTL 写入失败不消费。
- result 写入失败不应提前消费。
- 只有成功写入并可记录结果时才消费。

## 8. 高风险能力 gating

raw block 和 suspend 在 v1 中纳入架构，但默认不可普通执行。

执行条件：

- 当前 control mode 允许。
- 对应 capability 为 true。
- 写入前 backup 成功。
- PCTL adapter 执行成功。

capability 文件：

```json
{
  "version": 1,
  "raw_block_verified": false,
  "suspend_verified": false,
  "verified_at": {
    "raw_block": null,
    "suspend": null
  }
}
```

probe 请求：

- `probe_raw_block`
- `probe_suspend`

probe 必须在真机阶段单独执行。模拟器和 host tests 只能验证 gating 逻辑，不能声明真实能力已验证。

probe 成功后写入 capability，并记录 event：

- request id。
- control mode。
- PCTL adapter 返回值。
- 人工确认状态。
- 验证时间。

## 9. 错误码和日志

错误码集中维护，并且稳定。

错误码应覆盖：

- request/schema 错误。
- token 错误。
- control guard 错误。
- risky capability 错误。
- PCTL 错误。
- storage 错误。
- config/rules/state 错误。

每个错误码映射：

- numeric code。
- stable reason string。
- user-facing Chinese message。
- log detail。

日志分两层：

- `logs/sysmodule.log`：人类排障文本日志，可轮转。
- `logs/events.jsonl`：结构化审计日志，append-only，用于测试断言和问题复盘。

必须记录 event：

- 离线码成功。
- 离线码拒绝。
- 家长区操作。
- PCTL 写入成功或失败。
- backup 成功或失败。
- disable.flag 生效。
- unknown mode 降级。
- raw block / suspend probe。
- stuck processing request 恢复。

## 10. 测试架构

测试是本架构的一等目标，而不是实现后的补充。

### 10.1 Unit tests

必须覆盖：

- token encode/decode。
- HMAC 40-bit 校验。
- day index 计算。
- weekday 计算。
- bedtime 跨天判断。
- request/result schema validation。
- control mode plan。
- unlimited guard。
- raw/suspend capability guard。
- nonce 查重和消费时机。
- error code 映射。

### 10.2 Integration tests

集成测试使用：

- `mem_storage`
- `pctl_stub`
- deterministic fixtures

必须覆盖：

- 有效离线码 observe dry run。
- 有效离线码 grant 写入。
- grant 成功后重复码拒绝。
- 错日期拒绝。
- 错密钥拒绝。
- 超上限拒绝。
- 今日额度设置。
- 今日加时。
- 今日不限。
- 今日禁玩。
- 恢复周模板。
- 周模板应用。
- bedtime 应用。
- parent unlock start/end/expire。
- raw block 未验证拒绝。
- suspend 未验证拒绝。
- probe 后 raw block/suspend 允许。
- backup 失败拒绝写入。
- PCTL read/write 失败。
- storage 失败。
- request queue pending -> processing -> done -> result。
- stuck processing request 恢复。

### 10.3 Fixtures

`tools/make_fixtures.py` 负责生成固定测试码，避免手写授权码。

fixture 应固定：

- device id。
- grant secret。
- date/day index。
- nonce。
- minutes。
- expected code。
- expected result。

### 10.4 模拟器测试

模拟器只验证：

- companion NRO 启动。
- UI 输入流程。
- PIN 初始化和校验。
- request 文件写入。
- 手动 result 展示。
- 家长区请求生成。

模拟器不验证：

- boot2 sysmodule。
- 真实 PCTL IPC。
- raw block。
- suspend。
- play timer 真实行为。

### 10.5 真机测试阶段

真机验证必须阶段化：

1. host tests 全绿。
2. companion 模拟器通过。
3. NRO-only 安全包。
4. `disabled` boot2 冒烟。
5. `observe` 读取状态。
6. `grant` 1 分钟最小写入。
7. `grant` 拒绝用例。
8. weekly / bedtime / parent unlock。
9. `enforce` 强控制。
10. raw block / suspend probe 后单独验证。

22.5.0 的 PCTL 写入、raw block 和 suspend 行为必须以真机验证为准，不能只依赖旧代码注释。

## 11. 实施顺序

推荐实施顺序：

1. 建立 common core：error code、time math、token、schema、fixtures。
2. 建立 Storage/PCTL 抽象和 host test harness。
3. 实现 rule engine、control policy、nonce ledger。
4. 跑通 request queue integration tests。
5. 实现 sysmodule orchestration、SDMC storage、backup、event logging。
6. 实现真实 PCTL adapter，并先只用于 disabled/observe/grant 阶段验证。
7. 实现 companion RequestClient、孩子主界面、状态展示。
8. 实现家长区完整管控入口。
9. 加入 raw block/suspend probe 和 capability gating。
10. 按真机阶段清单逐级启用。

## 12. 后续优化

以下内容不阻塞 v1 架构稳定性：

- 更漂亮的 UI 和文件预览体验。
- 本地月报展示优化。
- 敏感字段遮罩和二次确认。
- 更多离线 token action。
- 自动清理历史 request/result 的策略细化。
- 桌面端协议调试工具增强。
- 更完整的安装包向导。

## 13. 关键假设

- 首版功能范围按完整本地管控设计，但高风险能力默认 gated。
- 稳定性优先于 UI 丰富度和高级报表。
- 架构文档保持高层评审稿风格，不展开到逐字段实现规格。
- 新项目不兼容旧数据、旧 token、旧 request/result 协议和旧目录。
- 22.5.0 的 PCTL 行为需要真机验证。

