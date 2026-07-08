# Nintendo Switch 家长控制离线加时 Clean-Slate 技术架构设计

本文档基于“新项目不需要兼容旧数据、旧 token、旧 JSON 文件和旧目录结构”的前提重新评估技术架构。目标是在实现相同产品能力的基础上，采用更干净的数据协议、更清晰的模块边界和更强的可测试性。

## 1. 重新评估结论

不需要兼容旧数据后，建议做以下变化：

- 不保留旧的 16 字符授权码格式，改为更强的 20 字符 Crockford Base32 授权码。
- 不保留旧的单文件 `grant_request.json` / `grant_result.json` 竞态模型，改为 request queue + request_id。
- 不保留旧的明文设置密码，改为 salted PIN hash，仍声明它只是本地 UI 保护。
- 不保留旧的字段散落式 JSON，所有数据文件从 v1 开始带 schema version。
- 不保留旧的 `date_key` 对外表示，内部统一使用 `day_index_since_2020`，展示层再格式化为日期。
- 不保留旧 token action 编号，可以重新定义稳定枚举。
- 不保留旧目录命名，可以使用更清晰的应用目录。
- 可以吸收另一份设计中的 `common/`、`IStorageLayer`、`IPctlAdapter`、集中错误码、桌面端测试和 fixtures 方案。

仍建议保留的产品级原则：

- 默认 observe，不直接写 PCTL。
- fail-open 优先。
- 写 PCTL 前必须备份。
- observe 不消费 nonce。
- 只有成功写入后才消费 nonce。
- raw block 和 suspend 必须真机验证后启用。
- 模拟器只测 NRO 和文件协议，真机才测 PCTL。

## 2. 技术选型建议

### 2.1 语言

推荐方案：

- sysmodule：C 或受限 C++。
- companion NRO：C 或受限 C++。
- common core：优先写成可 host-side 编译的纯 C/C++，不依赖 libnx。
- tools：Python。

如果使用 C++：

- 禁用 exceptions：`-fno-exceptions`
- 禁用 RTTI：`-fno-rtti`
- 避免复杂静态初始化。
- 避免在 sysmodule 热路径大量使用动态分配。
- 接口可以用 abstract class，但存储和 PCTL 层要有明确生命周期。

更稳妥的 Switch sysmodule 实现方式：

- 业务 core 可采用 C++ 提升可测试性。
- platform/sysmodule 边界保持简单。
- PCTL IPC、日志、文件 I/O 仍用薄封装，避免过度抽象。

## 3. 新项目目录结构

```text
switch-play-time-control-local/
├── common/
│   ├── token/
│   │   ├── token_payload.h
│   │   ├── token_codec.c/.h
│   │   ├── token_hmac.c/.h
│   │   └── crockford_base32.c/.h
│   ├── protocol/
│   │   ├── request.h
│   │   ├── result.h
│   │   ├── error_code.h
│   │   └── schema_version.h
│   ├── rules/
│   │   ├── time_rules.c/.h
│   │   ├── time_state.c/.h
│   │   └── rule_engine.c/.h
│   └── util/
│       ├── time_math.c/.h
│       └── fixed_string.c/.h
│
├── sysmodule/
│   ├── source/
│   │   ├── main.c
│   │   ├── service_lifecycle.c/.h
│   │   ├── request_dispatcher.c/.h
│   │   ├── control_policy.c/.h
│   │   ├── config_store.c/.h
│   │   ├── nonce_store.c/.h
│   │   ├── pctl_adapter.c/.h
│   │   ├── pctl_backup.c/.h
│   │   ├── storage_sdmc.c/.h
│   │   ├── event_logger.c/.h
│   │   ├── report_writer.c/.h
│   │   └── json_codec.c/.h
│   └── Makefile
│
├── companion/
│   ├── source/
│   │   ├── main.c
│   │   ├── app.c/.h
│   │   ├── request_client.c/.h
│   │   ├── local_auth.c/.h
│   │   ├── ui_child.c/.h
│   │   ├── ui_parent.c/.h
│   │   ├── ui_keyboard.c/.h
│   │   └── file_preview.c/.h
│   └── Makefile
│
├── tools/
│   ├── grant_code.py
│   ├── package_sdmc.sh
│   ├── protocol_probe.py
│   └── make_fixtures.py
│
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── fixtures/
│   ├── mocks/
│   │   ├── mem_storage.c/.h
│   │   └── pctl_stub.c/.h
│   └── Makefile
│
├── config.example.json
├── rules.example.json
├── docs/
│   ├── GRANTS.md
│   ├── TESTING.md
│   └── REAL_SWITCH_TESTING.md
├── README.md
└── Makefile
```

## 4. Clean-Slate SD 卡布局

建议使用更清晰的新目录：

```text
sdmc:/switch/play-time-control/
```

目录结构：

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
│   ├── events.jsonl
│   └── report.txt
├── ledger/
│   └── used_nonces.jsonl
├── backups/
│   └── last_pctl_backup.txt
└── flags/
    └── disable.flag
```

Atmosphere 路径：

```text
sdmc:/atmosphere/contents/<new-program-id>/exefs.nsp
sdmc:/atmosphere/contents/<new-program-id>/toolbox.json
sdmc:/atmosphere/contents/<new-program-id>/flags/boot2.flag
sdmc:/switch/play-time-control/pctc.nro
```

设计理由：

- `inbox/` 和 `results/` 避免单个 request/result 文件竞态。
- `logs/`、`ledger/`、`backups/` 分离，便于家长区文件预览和问题排查。
- `flags/disable.flag` 放到应用目录内部，语义更清晰。
- 新项目可以选新 Program ID，避免和旧项目安装路径冲突。

## 5. 授权码重新设计

### 5.1 目标

旧 16 字符 token 可用，但 32-bit MAC 偏短。新项目不需要兼容旧码，建议改为 20 字符 Crockford Base32：

```text
XXXXX-XXXXX-XXXXX-XXXXX
```

总容量：

- 20 个 Base32 字符 = 100 bit。
- payload 60 bit。
- signature 40 bit。

### 5.2 Payload v1

```text
version:              4 bit   // v1 = 1
action:               4 bit   // add_today_minutes = 1
minutes:             11 bit   // 1..1440
day_index_since_2020:16 bit   // enough for about 179 years
nonce:               25 bit   // random per code
--------------------------------
payload total:        60 bit
mac:                 40 bit   // HMAC-SHA256 truncated
--------------------------------
token total:         100 bit
```

HMAC 输入：

```text
"PTC1" || device_id || NUL || payload_bits_as_bytes
```

密钥：

```text
grant_secret UTF-8 bytes
```

设计理由：

- 20 字符仍适合手动输入。
- 40-bit MAC 比旧 32-bit 更稳。
- 25-bit nonce 足够日常离线授权防重放。
- day index 比 `YYYYMMDD` 更紧凑，并避免日期格式解析差异。
- HMAC domain prefix `"PTC1"` 防止未来协议混用。

### 5.3 Token action

```c
typedef enum {
    TOKEN_ACTION_ADD_TODAY_MINUTES = 1
} TokenAction;
```

未来如需更多离线授权动作，可增加：

- set today limit
- unlock until
- emergency disable

但 v1 建议只支持加时，避免短码权限过大。

## 6. 配置文件设计

### 6.1 `config.json`

```json
{
  "version": 1,
  "device_id": "kid-switch",
  "grant_secret": "replace-with-long-random-secret",
  "max_add_minutes": 120,
  "control_mode": "observe",
  "allow_unlimited_to_limited": false,
  "default_request_timeout_ms": 8000
}
```

### 6.2 `auth.json`

替代旧的明文 `settings.conf`。

```json
{
  "version": 1,
  "pin_hash": "hex-encoded-hmac-sha256",
  "pin_salt": "hex-encoded-16-bytes",
  "hash": "hmac-sha256",
  "updated_at": 1783526400
}
```

PIN 校验：

```text
pin_hash = HMAC-SHA256(pin_salt, UTF8(pin))
```

说明：

- 这仍不是强安全边界，因为 SD 卡可读写。
- 但比明文 PIN 更适合文件预览和日常误泄露场景。
- 缺失 `auth.json` 时，首次进入家长区要求设置新 PIN；不再默认长期使用 `1234`。
- 如需保留低门槛测试包，可由打包脚本生成测试 PIN `1234`。

### 6.3 `rules.json`

```json
{
  "version": 1,
  "week": [
    {"mode": "unlimited", "minutes": 120},
    {"mode": "limit", "minutes": 60},
    {"mode": "limit", "minutes": 60},
    {"mode": "limit", "minutes": 60},
    {"mode": "limit", "minutes": 60},
    {"mode": "limit", "minutes": 60},
    {"mode": "unlimited", "minutes": 120}
  ],
  "today_override": null,
  "bedtime": {
    "enabled": false,
    "start_min": 1260,
    "end_min": 480
  },
  "limit_action": "remind"
}
```

`today_override` 示例：

```json
{
  "day_index": 2380,
  "mode": "limit",
  "minutes": 90,
  "source": "parent"
}
```

### 6.4 `capabilities.json`

高风险能力单独存放，避免和普通 rules 混在一起。

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

### 6.5 `state.json`

```json
{
  "version": 1,
  "day_index": 2380,
  "parent_unlock": {
    "active": false,
    "until": 0
  },
  "bedtime_active": false,
  "last_applied": {
    "mode": "limit",
    "minutes": 90,
    "at": 1783526400
  }
}
```

## 7. Request Queue 协议

### 7.1 为什么不用单个 request 文件

旧模型只有一个 `grant_request.json` 和一个 `grant_result.json`，简单但有几个问题：

- 新请求可能覆盖旧请求。
- companion 可能读到上一轮 result。
- 无法区分超时后 sysmodule 是否晚到处理。
- 不方便做桌面端批量协议测试。

新设计使用 request queue。

### 7.2 写请求流程

companion 生成 request id：

```text
<unix_ms>-<random16>
```

写入临时文件：

```text
inbox/pending/<request_id>.json.tmp
```

完成后 rename：

```text
inbox/pending/<request_id>.json
```

sysmodule 处理时 rename：

```text
inbox/processing/<request_id>.json
```

处理完成：

```text
inbox/done/<request_id>.json
results/<request_id>.json
```

### 7.3 请求格式

```json
{
  "version": 1,
  "request_id": "1783526400123-a4f2",
  "type": "offline_code",
  "created_at": 1783526400,
  "payload": {
    "code": "ABCDE-FGHIJ-KLMNO-PQRST"
  }
}
```

家长区请求：

```json
{
  "version": 1,
  "request_id": "1783526400456-0b31",
  "type": "set_today_limit",
  "created_at": 1783526400,
  "payload": {
    "minutes": 90
  }
}
```

### 7.4 结果格式

```json
{
  "version": 1,
  "request_id": "1783526400123-a4f2",
  "type": "offline_code",
  "status": "ok",
  "mode": "grant",
  "dry_run": false,
  "applied": {
    "minutes": 30,
    "today_limit": 120
  },
  "state": {
    "limited_today": true,
    "blocked_today": false,
    "unrestricted_today": false,
    "remaining_available": true,
    "remaining_minutes": 45,
    "play_timer_enabled": true,
    "restricted_now": false,
    "day_of_week": 3,
    "day_index": 2380,
    "active_rule": "limit",
    "active_rule_minutes": 120,
    "bedtime_active": false,
    "parent_unlock_active": false
  },
  "capabilities": {
    "limit_action": "remind",
    "raw_block_verified": false,
    "suspend_verified": false
  },
  "completed_at": 1783526401
}
```

错误结果：

```json
{
  "version": 1,
  "request_id": "1783526400123-a4f2",
  "type": "offline_code",
  "status": "error",
  "mode": "grant",
  "error": {
    "code": 203,
    "reason": "bad_signature",
    "message": "授权码签名不匹配"
  },
  "completed_at": 1783526401
}
```

## 8. 集中错误码

```c
typedef enum {
    ERR_OK = 0,

    ERR_UNSUPPORTED_VERSION = 100,
    ERR_BAD_REQUEST = 101,
    ERR_UNKNOWN_REQUEST_TYPE = 102,
    ERR_REQUEST_EXPIRED = 103,

    ERR_BAD_CODE = 200,
    ERR_BAD_TOKEN_VERSION = 201,
    ERR_UNSUPPORTED_TOKEN_ACTION = 202,
    ERR_BAD_SIGNATURE = 203,
    ERR_BAD_CLOCK = 204,
    ERR_WRONG_DATE = 205,
    ERR_USED_TOKEN = 206,
    ERR_MINUTES_EXCEED_LIMIT = 207,

    ERR_DISABLED = 300,
    ERR_UNLIMITED_NOT_ALLOWED = 301,
    ERR_RAW_BLOCK_NOT_VERIFIED = 302,
    ERR_SUSPEND_NOT_VERIFIED = 303,

    ERR_PCTL_INIT_FAILED = 400,
    ERR_PCTL_READ_FAILED = 401,
    ERR_PCTL_WRITE_FAILED = 402,
    ERR_PCTL_BACKUP_FAILED = 403,

    ERR_STORAGE_READ_FAILED = 500,
    ERR_STORAGE_WRITE_FAILED = 501,
    ERR_CONFIG_INVALID = 502,
    ERR_RULES_INVALID = 503
} ErrorCode;
```

每个错误码映射：

- numeric code
- stable reason string
- user-facing Chinese message
- log detail

## 9. 核心模块边界

### 9.1 Common Token

纯逻辑模块，不依赖 libnx、不读写文件。

职责：

- encode/decode 20 字符 token。
- pack/unpack 100-bit token。
- HMAC-SHA256 truncated 40-bit。
- 校验 version/action/minutes/day_index。

### 9.2 Token Verifier

职责：

- 使用 common token 解析 code。
- 比对当前 day_index。
- 比对 HMAC。
- 检查 max_add_minutes。
- 查询 nonce store 是否已用。

不做：

- 不检查 unlimited guard。
- 不读写 PCTL。
- 不消费 nonce。

### 9.3 Control Policy

职责：

- 根据 `control_mode`、请求类型、PCTL 当前状态、rules、capabilities 生成执行计划。
- 处理 unlimited guard。
- 处理 raw/suspend verified guard。
- 判断 dry_run、是否写 PCTL、是否消费 nonce、是否需要备份。

核心结构：

```c
typedef struct {
    bool allowed;
    bool dry_run;
    bool writes_pctl;
    bool consumes_nonce;
    bool requires_backup;
    ErrorCode error;
    PctlTarget target;
} ControlPlan;
```

### 9.4 PCTL Adapter

职责：

- 所有 libnx PCTL IPC 的唯一入口。
- 读取当前状态。
- 写入目标状态。
- 启停 play timer。
- raw settings offset 封装在内部。

业务层不能直接访问 raw `u16[34]`。

### 9.5 Storage Interface

从另一份设计借鉴接口化思想：

```c
typedef struct Storage Storage;

typedef struct {
    bool (*read_text)(Storage *, const char *path, char *out, size_t out_size);
    bool (*write_text_atomic)(Storage *, const char *path, const char *text);
    bool (*append_line)(Storage *, const char *path, const char *line);
    bool (*remove)(Storage *, const char *path);
    bool (*exists)(Storage *, const char *path);
    bool (*list_json)(Storage *, const char *dir, char names[][128], size_t max, size_t *count);
} StorageVTable;

struct Storage {
    const StorageVTable *vtable;
    void *impl;
};
```

真机实现：

- SDMC storage。

测试实现：

- in-memory storage。

### 9.6 PCTL Interface

```c
typedef struct Pctl Pctl;

typedef struct {
    Result (*read_status)(Pctl *, PctlStatus *out);
    Result (*apply_target)(Pctl *, const PctlTarget *target, PctlBackup *backup);
    Result (*start_timer)(Pctl *);
    Result (*stop_timer)(Pctl *);
} PctlVTable;

struct Pctl {
    const PctlVTable *vtable;
    void *impl;
};
```

真机实现：

- libnx service IPC。

测试实现：

- pctl_stub，支持注入今天星期、当前限制、剩余时间、错误码。

## 10. 控制模式重新定义

```text
disabled:
  拒绝所有请求。
  不读 PCTL，不写 PCTL。
  可写日志。

observe:
  处理请求、验码、读 PCTL、计算预期结果。
  不写 PCTL。
  不消费 nonce。
  可更新纯本地 rules/state，但 result 必须标记 dry_run。

grant:
  有效授权码和家长区操作可写 PCTL。
  成功写入后消费 nonce。
  不在开机强制启用 play timer。

enforce:
  grant 的全部能力。
  开机确保 play timer 启用。
  写入后刷新 play timer。
  bedtime 强控制和 suspend 只在该模式允许。
```

注意：

- `disable.flag` 覆盖所有模式。
- 未知 mode 按 `observe`。
- `observe` 是否更新 rules/state 需要产品取舍。建议：家长区规则编辑可以写 rules，但不写 PCTL；结果中明确 `dry_run:true` 和 `pending_apply:true`。

## 11. Companion 重新设计

### 11.1 主界面

孩子可见：

- 今日状态。
- 剩余时间。
- 离线码输入。
- 最近结果。
- 刷新状态。

孩子不可见：

- 文件预览。
- grant secret。
- 高风险功能。
- 家长设置入口文案。

家长入口：

- 长按 `L + R + X`。
- PIN 校验。
- 首次启动要求设置 PIN。

### 11.2 Request Client

职责：

- 生成 request_id。
- 原子写入 pending queue。
- 等待同名 result。
- 超时后展示“后台未响应”，而不是“失败”。
- 支持查看历史 result。

建议：

- result 文件按 request_id 保存最近 N 个。
- companion 可清理过旧 request/result。
- sysmodule 启动时可清理 stuck processing 请求。

## 12. 测试架构

### 12.1 单元测试

必须覆盖：

- 20 字符 token 编解码。
- HMAC 40-bit 校验。
- day_index 计算。
- nonce 查重。
- max minutes。
- control mode plan。
- unlimited guard。
- raw/suspend guard。
- bedtime 跨天判断。
- request queue 状态迁移。
- error code 映射。

### 12.2 Integration 测试

使用：

- `mem_storage`
- `pctl_stub`
- 固定 fixtures

覆盖：

- 有效码 observe dry run。
- 有效码 grant 写入。
- grant 成功后 nonce 消费。
- 重复码拒绝。
- 错日期拒绝。
- 错密钥拒绝。
- 超上限拒绝。
- disable.flag 覆盖。
- parent unlock start/end/expire。
- bedtime remind。
- raw block 未验证拒绝。
- raw block probe 成功。

### 12.3 Fixtures

```json
{
  "version": 1,
  "device_id": "test-device",
  "grant_secret": "test-secret-do-not-use-in-prod",
  "cases": [
    {
      "name": "valid_30min",
      "minutes": 30,
      "date": "2026-07-08",
      "nonce": 4660,
      "code": "ABCDE-FGHIJ-KLMNO-PQRST",
      "expect": "ok"
    }
  ]
}
```

`tools/make_fixtures.py` 负责生成 deterministic code，避免手写短码。

### 12.4 真机阶段

仍保留阶段化：

1. desktop tests 全绿。
2. companion 模拟器测试。
3. disabled 真机 boot2 冒烟。
4. observe 真机读状态。
5. grant 1 分钟最小写入。
6. grant 拒绝用例。
7. enforce 强控制。
8. parent zone rules。
9. raw block / suspend probe。

## 13. 从另一份设计中采纳的内容

明确采纳：

- PC 侧 TokenEncoder / HmacSigner / Base32Formatter 分层。
- companion 的 ChildUI / ParentZone / RequestClient 分层。
- sysmodule 的 ServiceMain / RequestHandler / SecurityCtrl / PctlAdapter / RulesEngine / EventLogger / StorageLayer 分层。
- Storage 和 PCTL 抽象接口，支持 mem_storage 和 pctl_stub。
- 集中错误码。
- 桌面端单测构建。
- fixtures 驱动固定测试码。
- 阶段化启用顺序。

调整后采纳：

- C++ class 接口改为 C vtable 或受限 C++，避免 sysmodule runtime 风险。
- TokenPayload 不使用 `YYYYMMDD`，改用紧凑 day index。
- `UnlimitedNotAllowed` 不放在 TokenVerifier，改由 ControlPolicy 处理。
- request/result 不再是单文件，改为 queue + request_id。
- PIN 不再明文保存，改为 salted hash。

## 14. 推荐实施顺序

1. 先实现 common token v1 和 fixtures 生成器。
2. 实现 error code、request/result schema。
3. 实现 mem_storage 和 pctl_stub。
4. 实现 token verifier、nonce store、control policy。
5. 实现 rule engine 和 bedtime/parent unlock 状态。
6. 用 host-side integration test 跑完整 request queue。
7. 实现 sysmodule SDMC storage 和 PCTL adapter。
8. 实现 companion RequestClient 和主界面。
9. 实现家长区。
10. 真机按 disabled -> observe -> grant -> enforce 分阶段验证。

## 15. 最终建议

在“不兼容旧数据”的前提下，最佳方案不是在现有架构上小修小补，而是采用 clean-slate 数据协议：

- 新 20 字符 token。
- 新应用目录。
- 新 versioned JSON。
- 新 request queue。
- 新 error code。
- 新 auth hash。
- 新 host-side test harness。

这样能保留产品功能，但把旧项目中最容易出问题的部分，也就是手写协议、单文件请求竞态、明文 PIN、短 MAC、测试困难，一次性清掉。
