# Nintendo Switch 家长控制离线加时产品说明

本文档从当前项目实现与说明文档中提取产品级信息，用于后续重构或重新设计全新项目。它描述的是产品目标、核心功能、运行环境、系统边界、数据协议、安全原则和测试方式，而不是当前代码的逐行实现。

## 1. 产品定位

本产品是一个面向 Nintendo Switch 家长控制场景的离线加时与本地时间管理工具。它允许家长在没有网络、没有服务端、没有实时远程控制能力的情况下，给孩子临时增加当天可玩时间，并在 Switch 本地维护一套轻量的时间规则。

产品由三类组件组成：

- Switch 后台 sysmodule：随 Atmosphere boot2 启动，轮询 SD 卡请求文件，验证离线授权码，读取或写入 PCTL 家长控制 play timer。
- Switch 前台 companion NRO：给孩子提供离线码输入入口，给家长提供密码保护的本地时间管理界面。
- 家长侧离线码生成工具：在电脑上生成当天有效、一次性使用的短授权码。

核心价值：

- 离线可用：不依赖网络、账号、服务器或手机 App。
- 最小交互：孩子只需输入短码即可申请加时。
- 可控风险：通过 `disabled`、`observe`、`grant`、`enforce` 四档模式分阶段测试和启用。
- 可恢复：通过 `disable.flag` 和 boot2 flag 控制 sysmodule 是否影响系统。
- 本地透明：所有请求、结果、规则、事件、报告都保存在 SD 卡文件中，便于查看和调试。

## 2. 目标用户与使用场景

### 2.1 家长

家长需要：

- 为某台 Switch 配置设备标识和离线签名密钥。
- 在电脑上按分钟数生成当天有效的授权码。
- 在 Switch 上通过隐藏家长区设置今天额度、周模板、bedtime、临时解锁和限制动作。
- 在真实机器测试中可以安全地观察、回滚和停用模块。

### 2.2 孩子

孩子需要：

- 在 Switch 前台 NRO 输入家长给出的短授权码。
- 查看今日额度、剩余时间、当前状态和最近执行结果。
- 不需要知道家长设置密码。
- 不能从主界面直接看到或进入家长管理功能。

### 2.3 开发者/维护者

开发者需要：

- 能在模拟器验证 NRO 与 SD 卡文件协议。
- 能在真机分阶段验证 PCTL 读取、写入和强控制行为。
- 能通过日志、事件文件、结果 JSON 和备份文件定位问题。
- 能用固定日期和 nonce 生成可复现测试码。

## 3. 功能范围

### 3.1 离线授权码加时

功能说明：

- 家长侧工具生成 16 位 Crockford Base32 短码，显示形式类似 `28G1-PRF6-FTNN-PD9X`。
- 授权码包含版本、动作、分钟数、日期和 nonce。
- 授权码签名使用 `grant_secret` 作为 HMAC-SHA256 密钥，并截断为 32-bit signature。
- HMAC 输入为 `device_id || NUL || payload_bytes`。
- Switch 端验证成功后，为当天家长控制限制增加指定分钟数。
- 单个授权码可加分钟数受 `max_add_minutes` 限制。
- 授权码只对同一天、同一 `device_id`、同一 `grant_secret` 有效。
- 在 `grant`/`enforce` 模式成功写入后，日期和 nonce 会记录到 `used_grants.dat`，同一台 Switch 上不能重复使用。
- 在 `observe` 模式下可以验证授权码和计算预期结果，但不写 PCTL，也不消费 nonce。

主要拒绝原因：

- `bad_code`：授权码格式或解码失败。
- `bad_version`：版本不支持。
- `unsupported_action`：动作不支持。
- `bad_signature`：签名不匹配。
- `bad_clock`：无法取得可靠日期。
- `wrong_date`：授权码日期不是 Switch 当前日期。
- `used_token`：同一天同一 nonce 已成功使用。
- `minutes_exceed_limit`：授权分钟数超过配置上限。
- `unlimited_not_allowed`：今天原本无限制或未配置，且配置禁止从无限制转为有限制。
- `pctl_init_failed`、`pctl_write_failed`、`pctl_backup_failed` 等 PCTL 或备份失败。

### 3.2 前台 companion NRO

孩子主界面能力：

- 显示产品名称、今日额度、剩余时间、当前限制状态和最近执行结果。
- `A` 输入离线授权码。
- `B` 读取最近一次 sysmodule 执行结果。
- `Y` 刷新当前时间状态。
- `Plus` 退出。
- 长按 `L + R + X` 约 2 秒后，输入本地设置密码进入家长区。

约束：

- 输入离线码不需要密码。
- 家长区需要本地设置密码，默认 `1234`。
- 设置密码保存在 SD 卡明文文件 `settings.conf`，只能视作本地 UI 保护，不是强安全边界。
- 文件预览可能显示 `grant_secret` 和明文设置密码，只应在可信环境中使用。

### 3.3 家长区本地时间管理

家长区能力：

- 设置今日固定额度。
- 临时增加今日 15/30/60 分钟，或输入指定分钟数。
- 今日不限。
- 今日禁止游玩。
- 恢复周模板。
- 设置平日/周末或七天周模板。
- 设置 bedtime 开始/结束时间。
- 设置达到限制后的动作：提醒或 suspend。
- 启动/结束临时 parent unlock，用于暂停 play timer 一段时间。
- 查看配置文件、事件日志、本地状态和月度报告。
- 修改本地设置密码。
- 触发 raw block probe，并在真机验证后标记 `raw_block_verified=true`。

本地规则默认模型：

- 一周七天规则，天序为 Sun=0 到 Sat=6。
- 每天模式包括 `limit`、`unlimited`、`blocked`。
- 示例默认：周日/周六不限，周一到周五 60 分钟。
- 今日 override 可覆盖周模板，并带日期 key，避免跨天误用。
- bedtime 默认时间为 21:00 到 08:00，但默认未启用。
- 限制动作默认 `remind`。
- `raw_block_verified` 和 `suspend_verified` 默认都是 `false`。

### 3.4 后台 sysmodule

sysmodule 运行职责：

- 作为 Atmosphere boot2 sysmodule 启动。
- 初始化 SM、FS、Time、SDMC 等服务。
- 创建 `sdmc:/switch/pctltcp-sysmodule` 工作目录。
- 加载 `grant.conf`、`time_rules.json`、`time_state.json`。
- 读取系统时区，用于正确计算星期和日期 key。
- 按固定周期轮询 `grant_request.json`。
- 处理请求后删除请求文件并写入 `grant_result.json`。
- 写入 `sysmodule.log`，日志超过约 100 KiB 时轮转到 `.old`。
- 每 60 秒写入一次主循环心跳日志。
- 根据 `control_mode` 决定是否读取、写入或强制启用 PCTL play timer。

sysmodule 启动安全约束：

- boot2 场景下服务可能尚未就绪，当前实现启动后会先 sleep 约 15 秒再初始化服务。
- 初始化失败时仍进入请求循环，尽量保留日志和可恢复能力。
- `disable.flag` 优先级高于配置模式，存在时进入 fail-open。

### 3.5 PCTL 家长控制操作

PCTL 能力边界：

- 读取 play timer 是否启用。
- 启动/停止 play timer。
- 读取剩余时间。
- 判断当前是否被 play timer 限制。
- 读取/写入 play timer settings 原始结构。
- 设置今天限制分钟数。
- 设置今天不限。
- 设置今天 raw `0` 禁玩。
- 获取当前星期和日期 key。

已知实现细节：

- PlayTimerSettings 大小为 0x44 字节，即 `u16[34]`。
- Day 顺序为 Sun=0 到 Sat=6。
- 每天占 4 个 `u16` 槽位，包含 configured flag、restricted flag、minutes 和 padding。
- `0xFFFF` 表示 unlimited。
- raw `0` 表示 blocked，但风险较高，必须经过真机 probe 验证。
- 写入前应创建 `last_pctl_backup.dat`，记录旧值、新值和原始 raw hex，便于人工恢复。

## 4. 控制模式

`control_mode` 是重构设计中必须保留的核心安全机制。

| 模式 | 授权请求 | PCTL 读取 | PCTL 写入 | 开机 play timer | nonce |
| --- | --- | --- | --- | --- | --- |
| `disabled` | 拒绝，返回 `disabled` | 不读取 | 不写入 | 不启用 | 不消耗 |
| `observe` | 验证并返回 `dry_run:true` | 读取 | 不写入 | 不启用 | 不消耗 |
| `grant` | 有效码写入当天限制 | 读取 | 写入 | 不主动启用 | 成功写入后消耗 |
| `enforce` | 等同 grant | 读取 | 写入 | 开机尝试启用，写入后刷新 | 成功写入后消耗 |

设计原则：

- 新项目默认应使用 `observe`，而不是直接写 PCTL。
- 真机测试应从 `disabled` 或 `observe` 开始。
- 只有 `grant`/`enforce` 成功写入后才消费 nonce。
- 未知 `control_mode` 应降级为 `observe`。
- `disable.flag` 应覆盖所有模式，强制进入 fail-open。

## 5. 配置与文件协议

### 5.1 SD 卡目录

统一工作目录：

```text
sdmc:/switch/pctltcp-sysmodule/
```

主要文件：

```text
grant.conf
settings.conf
grant_request.json
grant_result.json
used_grants.dat
disable.flag
last_pctl_backup.dat
time_rules.json
time_state.json
events.jsonl
monthly_report.txt
sysmodule.log
```

Atmosphere 安装路径：

```text
sdmc:/atmosphere/contents/010000000000BD23/exefs.nsp
sdmc:/atmosphere/contents/010000000000BD23/toolbox.json
sdmc:/atmosphere/contents/010000000000BD23/flags/boot2.flag
sdmc:/switch/pctltcp-grant.nro
```

### 5.2 `grant.conf`

示例：

```json
{
  "device_id": "kid-switch",
  "grant_secret": "change-me-to-a-long-random-secret",
  "max_add_minutes": 120,
  "control_mode": "observe",
  "allow_unlimited_to_limited": false
}
```

字段：

- `device_id`：设备标识，生成离线码时必须一致。
- `grant_secret`：离线码签名密钥，应使用足够长的随机字符串。
- `max_add_minutes`：单个离线码允许增加的最大分钟数。
- `control_mode`：控制 PCTL 触碰强度。
- `allow_unlimited_to_limited`：是否允许把当天无限制/未配置状态转为有限制。

### 5.3 `settings.conf`

内容为本地设置密码，默认：

```text
1234
```

缺失或为空时，companion 使用默认密码 `1234`。

### 5.4 `grant_request.json`

由 companion 写入，sysmodule 轮询读取并删除。

常见请求：

```json
{"type":"offline_code","code":"20F0-SSGJ-6JEA-AEJE"}
{"type":"status"}
{"type":"set_today_limit","minutes":90}
{"type":"add_today_minutes","minutes":15}
{"type":"disable_today_limit"}
{"type":"block_today"}
{"type":"probe_raw_block"}
{"type":"restore_today_policy"}
{"type":"set_bedtime","enabled":true,"start_min":1260,"end_min":480}
{"type":"set_limit_action","action":"remind","raw_block_verified":false,"suspend_verified":false}
{"type":"parent_unlock_start","minutes":30}
{"type":"parent_unlock_end"}
```

周模板请求使用 `set_weekly_template`，包含 `day0_mode` 到 `day6_mode` 和 `day0_minutes` 到 `day6_minutes`。

### 5.5 `grant_result.json`

由 sysmodule 写入，companion 读取展示。

成功结果包含：

- `status:"ok"`
- `applied_minutes`
- `today_limit`
- `dry_run`
- `mode`
- `limited_today`
- `blocked_today`
- `unrestricted_today`
- `remaining_available`
- `remaining_minutes`
- `play_timer_enabled`
- `restricted_now`
- `today`
- `date_key`
- `active_rule`
- `active_rule_minutes`
- `bedtime_active`
- `parent_unlock_active`
- `limit_action`
- `raw_block_verified`
- `suspend_verified`

错误结果包含：

```json
{"status":"error","reason":"bad_signature","mode":"grant"}
```

### 5.6 `time_rules.json`

保存本地时间规则：

- `version`
- `today_override`
- `today_date_key`
- `today_mode`
- `today_minutes`
- `day0_mode` 到 `day6_mode`
- `day0_minutes` 到 `day6_minutes`
- `bedtime_enabled`
- `bedtime_start_min`
- `bedtime_end_min`
- `limit_action`
- `raw_block_verified`
- `suspend_verified`

### 5.7 `time_state.json`

保存运行态：

- `date_key`
- `parent_unlock_active`
- `parent_unlock_until`
- `bedtime_active`
- `last_applied_limit`

### 5.8 `events.jsonl`

每行一个 JSON 事件，用于审计和本地报告。事件包括：

- 离线码成功或拒绝。
- 家长区手动操作。
- bedtime 进入/退出。
- parent unlock 开始、结束、过期。
- raw block probe 与 block 请求。
- 错误和拒绝原因。

### 5.9 `monthly_report.txt`

本地估算月报，不是 Nintendo 官方 per-game 月报。内容来源于本地事件和 PCTL 剩余时间快照，主要统计：

- 总事件数。
- 成功离线授权次数。
- 手动时间操作次数。
- block/probe 操作次数。
- 拒绝或错误次数。
- 当前模式、限制动作和验证开关状态。

## 6. 家长侧工具

命令示例：

```bash
python tools/grant_code.py --minutes 30 --device kid-switch --secret change-me-to-a-long-random-secret
```

测试可复现命令：

```bash
python tools/grant_code.py \
  --minutes 30 \
  --device kid-switch \
  --secret change-me-to-a-long-random-secret \
  --date 2026-07-06 \
  --nonce 0x1234
```

工具要求：

- 支持指定分钟数。
- 支持指定设备 ID。
- 支持指定签名密钥。
- 默认使用当天日期。
- 默认随机 nonce。
- 支持固定日期和 nonce，便于测试。
- 输出第一行应为可直接告诉孩子输入的短码。

## 7. 构建、安装与部署

构建环境：

- devkitPro
- libnx
- 已设置 `DEVKITPRO`
- GNU Make
- Python，用于家长侧工具
- Bash，用于打包脚本

构建命令：

```bash
make
```

构建产物：

```text
pctltcp-sysmodule.nsp
companion/pctltcp-grant.nro
```

清理：

```bash
make clean
```

打包：

```bash
./tools/package_sdmc.sh
```

推荐保留的打包模式：

- `--safe-test`：安全测试包，不包含 `boot2.flag`，默认 `control_mode=observe`。
- `--boot2`：包含 boot2 flag，使 sysmodule 开机自启。
- `--control-mode <mode>`：写入指定控制模式。
- `--strong-control`：强控制快捷参数，包含 boot2 flag，并设置 `control_mode=enforce`。
- `--max-add-minutes <n>`：设置加时上限。
- `--no-allow-unlimited-to-limited`：保持无限制保护。

部署后需要重启 Switch 或重新进入 CFW。

## 8. 系统环境与兼容性

### 8.1 Switch 真机

需要：

- Nintendo Switch 真机。
- Atmosphere CFW。
- 可读写 SD 卡。
- Homebrew Menu，用于启动 companion NRO。
- PCTL 家长控制服务可用。

真机才能验证：

- boot2 sysmodule 自动启动。
- PCTL play timer 是否可以读取和写入。
- 今天限制是否真实增加。
- play timer 是否真实启用、暂停、恢复。
- raw `0` 禁玩和 suspend 的真实系统行为。

### 8.2 模拟器

Ryujinx/Eden 等模拟器可验证：

- NRO 能否启动。
- 菜单、输入、按键和软键盘流程。
- 设置密码读写。
- 请求 JSON 是否写入 SD 卡。
- 手动创建结果 JSON 后，NRO 是否能读取和展示。
- 家长区是否能写出对应请求。

模拟器通常不能完整验证：

- boot2 sysmodule 自动运行。
- PCTL 真实 IPC 行为。
- 系统家长控制时间真实变化。
- raw block 或 suspend 行为。

## 9. 安全设计原则

重构时应保留以下原则：

- 默认低入侵：默认 `control_mode=observe`，默认不打包 `boot2.flag`。
- fail-open：出现异常或检测到 `disable.flag` 时，停止处理授权并避免触碰 PCTL。
- 写入前备份：任何 PCTL 写入前都应保存 `last_pctl_backup.dat`。
- 分阶段启用：先 NRO-only，再 disabled，再 observe，再 grant，最后 enforce。
- 不把无限制意外改成有限制：`allow_unlimited_to_limited` 默认 `false`。
- 高风险能力必须验证：raw `0` 禁玩和 suspend 默认不可直接使用，应通过真机 probe 后启用。
- 密钥不入库：不得提交真实 `grant_secret` 或真实家庭配置。
- 本地密码不承诺强安全：`settings.conf` 明文保存，只保护 UI 入口。
- 日志可审计：授权成功、拒绝、家长操作、错误都应写事件日志。
- 错误不污染成功路径：无效码不应消费 nonce，不应修改 PCTL。

## 10. 测试方式与通过原则

### 10.1 模拟器测试原则

目标是验证前台和文件协议，不验证真实控制。

通过条件：

- `pctltcp-grant.nro` 可启动。
- 主菜单和按键提示正确。
- 默认密码 `1234` 可进入家长区。
- 修改密码后旧密码失败、新密码成功。
- 输入离线码会写出 `grant_request.json`。
- 手动写入 `grant_result.json` 后，NRO 能读取并展示。
- 家长区操作能写出对应请求 JSON。
- 模拟器中等待 sysmodule 超时属于预期行为。

### 10.2 真机测试原则

目标是从无风险到强控制逐步验证。

推荐阶段：

1. NRO-only：不放 `boot2.flag`，只测试 companion。
2. `disabled`：sysmodule 启动冒烟，只写日志和拒绝请求。
3. `observe`：验证码、读 PCTL、返回 `dry_run:true`，不改系统设置。
4. `grant`：用 1 分钟码做最小真实写入。
5. `grant` 拒绝用例：重复码、错日期、错密钥、超上限、无限状态保护。
6. `enforce`：在 grant 稳定后测试开机启用 play timer 和写入后刷新。
7. 家长区本地规则：今日操作、周模板、bedtime、parent unlock、日志和报告。
8. raw `0` 禁玩：先拒绝，再 probe，人工确认后才允许普通 block。

真机通过条件：

- sysmodule 开机自动运行并写日志。
- `disabled` 不触碰 PCTL。
- `observe` 返回 `dry_run:true`，系统家长控制不变化。
- `grant` 有效码真实加时，生成备份，并消费 nonce。
- 重复码返回 `used_token`。
- 错日期、错密钥、超上限、无限状态保护均返回对应 reason，且不修改 PCTL。
- `enforce` 能启用或刷新 play timer。
- 家长区隐藏入口和密码保护生效。
- 本地规则文件、事件日志、月度报告可正常维护。
- raw block 未验证前被拒绝，验证后才允许使用。

### 10.3 固定测试码原则

离线授权测试应使用固定日期和 nonce 生成可复现测试码，验证：

- 有效码。
- 重复码。
- 非当天日期码。
- 错误密钥码。
- 超过 `max_add_minutes` 的码。
- `observe` 下重复输入不消费 nonce。
- `grant`/`enforce` 成功写入后重复输入被拒绝。

## 11. 重构建议

全新项目设计时建议将系统拆成更清晰的边界：

- 授权码域：编码、签名、日期、nonce、防重放。
- 请求协议域：request/result JSON schema 和版本兼容。
- PCTL 适配层：所有 Switch IPC 和 raw settings 细节集中封装。
- 时间规则域：周模板、今日 override、bedtime、parent unlock。
- 安全控制域：control_mode、fail-open、备份和高风险能力验证。
- UI 域：孩子主屏、家长区、密码保护、文件预览。
- 测试工具域：固定测试码、模拟器请求/结果注入、真机阶段清单。

优先保留的兼容契约：

- SD 卡目录和核心文件名。
- `grant.conf` 基本字段。
- `grant_request.json` / `grant_result.json` 的 request-response 文件协议。
- `control_mode` 语义。
- `disable.flag` fail-open 行为。
- 授权码日期、nonce、HMAC 设计。
- `used_grants.dat` 防重复使用语义。
- `last_pctl_backup.dat` 写入前备份语义。

可以在重构中改进的方向：

- 引入严格 JSON parser 和 schema validation，替换手写 JSON 查找。
- 为请求和结果增加显式 `version` 字段。
- 将错误码定义成稳定枚举并集中维护。
- 将时间规则和授权码处理拆成可单元测试的纯逻辑模块。
- 提供桌面端或脚本化测试器，自动生成请求文件、读取结果并断言。
- 为真机测试生成阶段化检查报告。
- 对敏感文件预览增加更明显的遮罩或二次确认。
- 将 raw PCTL 写入能力放在独立高风险模块，并要求显式启用。

