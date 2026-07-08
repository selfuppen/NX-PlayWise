# Nintendo Switch 22.5.0 小孩使用时间管控总结

本文基于 Nintendo Switch 22.5.0 系统公开说明、Nintendo Switch Parental Controls 官方说明，以及当前仓库代码/文档整理。结论分为两层：

- 官方家长控制 App 在 22.5.0 系统上的能力。
- 当前 `switch-pctltcp-offline-grant` 项目通过 PCTL play timer 能做到的能力。

## 结论概览

22.5.0 系统下，小孩使用时间管理可以做到：

1. 使用官方家长控制 App 设置每日游玩时长上限。
2. 达到上限后只提醒，或开启 `Suspend Software` 让游戏自动暂停/关闭。
3. 按星期设置不同的每日时长。
4. 设置 bedtime 和次日早上重新允许游玩的时间。
5. 当天临时扩展游玩时间，或当天关闭限制。
6. 查看游玩记录和月度报告。
7. 家长临时输入 PIN 解除限制，解除期间 play timer 暂停计时。
8. 通过当前项目离线发放“今日加时码”，让孩子在无网络环境下临时增加当天可玩时间。
9. 当前项目底层具备读取剩余时间、是否已受限、读取/写入按星期分钟配置的代码能力，但对外产品化暴露的是“今日离线加时”。

需要注意：

- 官方家长控制的时间限制是整台主机级别，不是按每个用户分别设置。
- 当前项目不是官方家长控制 App 的完整替代品。
- 当前项目源码注释写明 PCTL IPC 在 22.1.0 上验证过；22.5.0 虽然官方更新说明未显示家长控制相关变更，但仍需要真机验证写入 PCTL 设置是否稳定可用。

## 官方家长控制 App 能做的时间管控

### 每日游玩时长限制

官方 App 可以为主机设置每日可玩时长。达到限制后，主机会在屏幕顶部显示提醒。

适用场景：

- 每天最多玩 30 分钟、60 分钟、120 分钟等。
- 家长希望先提醒孩子主动停止，而不是立即中断。

限制：

- 不能对同一台主机上的不同用户设置不同时间限制。
- 多个孩子共用一台主机时，官方建议按最小孩子的规则设置。

### 达到上限后强制暂停软件

官方 App 支持开启 `Suspend Software`。开启后，达到每日游玩时间上限时，主机会自动暂停/关闭正在运行的软件。

适用场景：

- 孩子不容易遵守提醒，需要硬性停止游戏。
- 家长希望时间到了自动执行，不依赖孩子主动退出。

注意：

- 如果不开启 `Suspend Software`，限制更偏向提醒。
- 开启后对所有用户的总游玩时间生效。

### 按星期设置不同限制

官方 App 支持为一周中的不同日期设置不同限制。

适用场景：

- 周一到周五 30 分钟。
- 周末 90 分钟。
- 节假日临时放宽。

### 设置 bedtime 和次日早上允许时间

官方支持页说明，App 可以设置 bedtime，让主机在某个晚间时间点停止游玩；同时可以设置次日早上从哪个时间点起重新允许游玩。官方说明还指出，如果同时设置了 play time limit 和 bedtime，主机会使用先到达的限制。

适用场景：

- 晚上 21:00 后不能继续玩。
- 次日早上 8:00 之后才允许恢复。
- 平日用 bedtime 管晚上，周末用每日时长管总量。

当前项目状态：

- 当前仓库没有实现 bedtime、早上允许时间或时间段窗口写入。
- 当前项目只处理每日分钟上限和离线加时。

### 当天临时扩展或关闭限制

官方支持页说明，家长可以把当天 play-time limit 临时扩展 5、15、30 分钟或 1 小时，也可以当天关闭限制。

当前项目与此类似，但实现方式不同：

- 官方方式依赖官方 App 或 PIN。
- 当前项目依赖离线授权码。
- 当前项目的加时时长由家长生成码时指定，并由 `max_add_minutes` 限制。

### 临时解除家长控制

家长输入 PIN 后，可以临时解除家长控制。临时解除期间，play timer 会暂停计时，主机限制也会解除。进入睡眠模式后，家长控制会自动重新启用。

适用场景：

- 家长自己玩游戏，不消耗孩子当天额度。
- 临时调试、安装、维护主机。

### 游玩记录与月度报告

官方 App 可以查看每个用户玩了哪些游戏、玩了多久，并提供月度报告。

适用场景：

- 了解孩子主要在玩哪些游戏。
- 复盘每月总使用情况。
- 发现异常使用时间。

当前项目状态：

- 当前项目不做游玩记录统计。
- 当前项目不生成月报。

## 当前项目能做的时间管控

### 开机自动启用 PCTL play timer

`source/main.c` 中的 sysmodule 会在 boot2 启动后初始化，并调用 PCTL play timer 相关接口，尝试确保 play timer 已启用。

意义：

- 系统启动后后台常驻。
- 不依赖孩子手动打开前台 NRO。
- 后台轮询离线授权请求。

### 离线加时码

当前项目的核心能力是“今日离线加时”：

1. 家长用 `tools/grant_code.py` 生成短授权码。
2. 孩子在 Switch 上打开 `companion/pctltcp-grant.nro`。
3. 孩子输入授权码。
4. companion 写入 `grant_request.json`。
5. sysmodule 验证授权码。
6. 验证通过后，读取今天的 PCTL 限制分钟数，并增加指定分钟。

示例：

```bash
python tools/grant_code.py \
  --minutes 30 \
  --device kid-switch \
  --secret change-me-to-a-long-random-secret
```

### 加时安全约束

当前项目对离线加时做了这些限制：

- 授权码只对当天有效。
- 授权码绑定 `device_id`。
- 授权码使用 `grant_secret` 做 HMAC-SHA256 签名。
- 同一个日期和 nonce 的授权码在同一台 Switch 上只能使用一次。
- 单个授权码允许增加的分钟数受 `max_add_minutes` 限制。
- 最终当天限制最多夹到 1440 分钟。

适用场景：

- 没有网络时，家长通过电话/短信/聊天工具给孩子一个今日加时码。
- 家长不想把官方 PIN 告诉孩子。
- 希望授权码一次性、当天有效。

### 按当天增加限制分钟数

当前业务动作是 `GRANT_ACTION_ADD_TODAY`，也就是只增加今天的可玩时间。

例如当前今天限制是 60 分钟，孩子输入 30 分钟授权码后，今天限制变为 90 分钟。

当前不会：

- 修改明天或未来日期的限制。
- 修改整周模板。
- 设置允许开始时间。
- 设置睡前禁止时间段。

### 底层具备的 PCTL 能力

`source/pctl_handler.*` 里已经封装了以下 PCTL 能力：

- 启动 play timer。
- 停止 play timer。
- 判断 play timer 是否启用。
- 获取 play timer 剩余时间。
- 判断当前是否已被 play timer 限制。
- 获取 play timer settings。
- 写入 play timer settings。
- 读取某一天的限制分钟数。
- 设置某一天的限制分钟数。
- 设置一周每天相同的限制分钟数。
- 根据系统日期计算今天是周几。

这些能力说明当前项目可以继续扩展为更完整的“本地时间规则管理器”，但目前对孩子暴露的前台界面只做离线码输入和设置密码修改。

## 当前项目不能或尚未实现的管控

### 不能按用户分别限时

官方家长控制本身也说明时间限制是主机级总时间，不支持同一台主机上不同用户分别设置不同时间上限。

当前项目沿用 PCTL play timer，因此同样不能实现可靠的 per-user 时间额度。

### 不能设置 bedtime 或每日允许开始时间

官方 App 可以设置 bedtime 和早上允许恢复时间，但当前项目没有对应实现。

如果要做，需要继续研究 22.5.0 下相关 PCTL/官方配置存储或 IPC。

### 当前不能设置“0 分钟硬封禁”

源码注释里的 PCTL 原始布局提到 `day minutes = 0` 表示 blocked，`0xFFFF` 表示 unlimited。但当前 helper 的实现里，传入 `minutes == 0` 会写成 `PT_DAY_NOLIMIT`，也就是无限制。

因此，以当前封装来看，不能通过现有公开函数直接设置“今天 0 分钟完全禁止”。如果要支持，需要新增明确的 block API，并在 22.5.0 真机验证 raw setting 写入效果。

### 不能控制具体游戏或 App

当前项目不做：

- 指定某个游戏限时。
- 指定某个游戏禁止运行。
- 按标题统计或授权。
- 按游戏类型区分额度。

### 不能替代年龄分级、社交、通信、VR 等限制

官方家长控制可以设置年龄分级和部分功能限制，例如通信、社交发布、VR Mode 等。当前项目没有实现这些设置。

### 不能替代 eShop 购买限制

eShop 购买限制通常应在 Nintendo Account 侧配置。当前项目不管理购买、支付、商店访问等规则。

### companion 设置密码不是强安全边界

`settings.conf` 中的 companion 设置密码用于保护本地设置页，默认是 `1234`，并以明文保存在 SD 卡上。

它适合防止误操作，不适合作为强安全机制。

## 22.5.0 兼容性判断

Nintendo 官方 22.5.0 更新说明显示，该版本主要包含 eShop 布局、身份认证 PIN、视频快退/快进和稳定性改进，没有列出家长控制 play timer 相关变更。

当前仓库代码注释写明 PCTL IPC 命令在 22.1.0 上验证过：

- `1451`: StartPlayTimer
- `1452`: StopPlayTimer
- `1453`: IsPlayTimerEnabled
- `1454`: GetPlayTimerRemainingTime
- `1455`: IsRestrictedByPlayTimer
- `145601`: GetPlayTimerSettings
- `195101`: SetPlayTimerSettingsForDebug

因此对 22.5.0 的判断是：

- 读取类能力大概率仍可用，但需要真机验证。
- 写入 `SetPlayTimerSettingsForDebug` 风险更高，因为项目头文件也提示写命令可能需要更高权限。
- 不能只凭 22.1.0 注释断言 22.5.0 完全兼容。

## 推荐使用方式

推荐把官方能力和当前项目组合使用：

1. 用官方 Nintendo Switch Parental Controls App 作为基础管控。
2. 开启每日游玩时长限制。
3. 需要强制停玩时开启 `Suspend Software`。
4. 使用官方 App 设置平日/周末不同规则。
5. 不把官方 PIN 告诉孩子。
6. 当前项目只作为“离线临时加时”通道。
7. 每次加时通过短码发放，受当天、设备、密钥、nonce 和 `max_add_minutes` 限制。

这样能保证基础规则仍由官方家长控制兜底，而离线加时由当前项目完成。

## 后续可扩展方向

如果要把当前项目从“离线加时工具”扩展为“完整本地时间管理工具”，可以考虑：

- 增加家长端本地设置页，用密码保护。
- 支持设置今天的固定限制分钟数，而不仅是加时。
- 支持设置一周七天模板。
- 增加明确的“今日禁止游玩”模式，并真机验证 raw `0` blocked 行为。
- 显示当前剩余可玩时间。
- 显示当前是否已被 play timer 限制。
- 支持查看最近授权记录。
- 支持撤销或降低今日额度，但要特别处理孩子已使用时间和 PCTL 行为。
- 研究 22.5.0 下官方 bedtime 和早上允许时间的配置路径或 IPC。

## 验证清单

在 22.5.0 真机上至少验证：

- sysmodule 是否 boot2 自动启动。
- `pctl_init()` 是否成功。
- `pctl_start_play_timer()` 是否成功。
- `pctl_get_daily_limit_minutes()` 是否能读到今日限制。
- 有效加时码是否能写入新的今日限制。
- 超过 `max_add_minutes` 的码是否被拒绝。
- 重复码是否被拒绝。
- 错日期码是否被拒绝。
- 错密钥码是否被拒绝。
- 达到限制后，官方 `Suspend Software` 行为是否仍按预期执行。

## 参考资料

- Nintendo Switch System Update Information: https://www.nintendo.com/en-gb/Support/Troubleshooting/Nintendo-Switch-System-Update-Information-1445507.html
- Nintendo Switch Parental Controls 官方说明: https://www.nintendo.com/my/support/switch/parentalcontrols/app/index.html
- Nintendo Support - How to Set Up, Adjust, or Remove Parental Controls: https://www.nintendo.com/en-gb/Support/Parental-Controls/How-to-Set-Up-Adjust-or-Remove-Parental-Controls-on-Nintendo-Switch-1494771.html
- Nintendo Support - Parental Controls Overview/FAQ: https://www.nintendo.com/en-gb/Support/Parental-Controls/Parental-Controls-for-Nintendo-Switch-2-and-Nintendo-Switch-Overview-FAQ-1494768.html
- Nintendo Switch Parental Controls App Store 说明: https://apps.apple.com/ni/app/nintendo-switch-parental-cont/id1190074407
- libnx pctl.h 文档: https://switchbrew.github.io/libnx/pctl_8h.html
- 当前仓库源码: `source/main.c`, `source/pctl_handler.c`, `source/pctl_handler.h`, `source/grant_manager.c`, `tools/grant_code.py`
