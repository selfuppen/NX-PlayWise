<div align="center">
  <img src="tools/ptc_frontend/playwise_logo.svg" alt="任我玩 PlayWise" width="180">

  # 任我玩 · PlayWise

  **Play Wise. Play More.**

  [English](README_en.md) | [简体中文](README.md)
</div>

任我玩（项目仓库名 NX-PlayWise）是面向已安装自定义固件（推荐 Atmosphère）的 Nintendo Switch 本地游玩时间管理工具。家长可设置每周、节假日等额度计划，也可生成当天有效的 8 位加时码供孩子在 Switch 上兑换。生成和兑换无需 Switch 联网，也不需要 Nintendo Account 或 PlayWise 服务器。

## 设计理念

固定计划让日常安排清晰、可预期；临时调整则为特殊情况留出弹性。即使家长不在身边，也能按约定把额外游玩时间作为奖励交给孩子自主领取，而不必反复修改长期规则。临时调整只在当天生效，次日继续按原计划执行。

> [!WARNING]
> 未破解的零售主机无法使用。PlayWise 依赖 Nintendo 官方家长控制正常计时，安装前确认官方家长控制已开启，并用非关键游戏验证计时和到时限制效果。限制生效后，PlayWise 主机应用可能无法打开；请事先安装并试用游戏内浮窗（Tesla Overlay）。

## 快速开始

1. 开启官方家长控制：设置 → 家长监护设置（Parent Control 设置）→ Parental Controls设置（监护人实施的使用限制）→ 未持有智能手机 → 下一步 → 下一步。按系统提示完成后，验证亮屏使用会计入额度、到时会限制使用；首次设置也可打开“开启家长控制说明”查看步骤。
2. 准备 Atmosphère、Homebrew Menu，以及 Ultrahand 或其他 Tesla 浮窗管理器。PlayWise 安装包不包含浮窗管理器。
3. 首次使用下载 `playwise-complete-<版本>.zip`：其中的 `playwise-<版本>.zip` 安装 Switch 端，`playwise-offline.html` 是备用的离线家长网页。将标准包内的 `atmosphere` 和 `switch` 目录合并到 SD 卡根目录后重启。
4. 从 Homebrew Menu 打开“任我玩”，按三步向导选择语言并核对环境、设置任我玩家长密码、确认启用或进入支持排障。孩子需要加时时，按下文选择主机应用或游戏内浮窗兑换。

完整的[安装、首次设置、使用与升级步骤](docs/使用指南.md)见使用指南。


## 加时码操作演示

### 孩子使用加时码：受限时用游戏内浮窗

> 使用加时码：在 Switch 生成或兑换加时码均无需联网。

每日额度耗尽后，Nintendo 原生弹窗可能使 Homebrew 和 PlayWise 主机应用无法继续使用。用预先设置的 Ultrahand/Tesla 快捷键打开浮窗管理器，选择“任我玩”（`playwise.ovl`）；输入家长给的 8 位码，核对预计结果并确认兑换。家长还可在浮窗中输入任我玩家长密码，单次授权增加分钟或设为今日不限时。就寝限制可跳过本次、关闭计划或恢复安装前设置。

<details open>
<summary>查看游戏内浮窗操作示意</summary>

| ![历史真机示意：在 Ultrahand 中选择任我玩浮窗](docs/images/usage/overlay/ultrahand-entry.jpg) | ![历史真机示意：在 HOME 上打开 PlayWise 浮窗输入加时码](docs/images/usage/overlay/playwise-code-entry-legacy.jpg) |
| :---: | :---: |

</details>

> 两图是旧版真机操作示意，分别展示浮窗管理器入口和 PlayWise 浮窗可覆盖 HOME 打开；当前按钮、确认页和可用操作以已安装版本为准。它们不是当前候选的真机验收截图。详见[受限时使用游戏内浮窗](docs/使用指南.md#受限时使用游戏内浮窗)。

### 家长生成加时码：手机扫码

> 生成加时码：
> 使用公开家长网页扫二维码跳转到生成时码的网页时，家长的手机或电脑需要能访问 github；
> 也可以使用 `playwise-complete-<版本>.zip` 安装包里面的 `playwise-offline.html` 备用的离线家长网页，此时手机/电脑也无需联网，配置好后可以长期使用（生成加时码需要的 key 和 id 存到浏览器里面了），步骤和密钥保护说明见[使用指南：在手机或电脑上生成](docs/使用指南.md#在手机或电脑上生成)。

在 Switch 家长区打开“离线加时 → 手机/电脑生成”，验证任我玩家长密码后显示配对二维码。用可信的家长手机或电脑扫码，在打开的[家长网页](https://selfuppen.github.io/NX-PlayWise/)确认“导入此设备”；选好与 Switch 本地日期一致的日期和加时分钟数，即可在浏览器中生成 8 位码并告诉孩子。网页在浏览器本地计算代码，不向 PlayWise 业务后端提交密钥或代码，不用每次扫码，可反复生成。

<details open>
<summary>查看手机扫码配对与家长网页示意</summary>

| ![手机或电脑扫码配对页面，二维码使用公开演示配置](docs/images/usage/parent/pairing-qr-demo.png) | ![家长网页生成加时码的历史界面示意](docs/images/usage/parent/web-code-demo.jpg) |
| :---: | :---: |

</details>

第一张图中的二维码已替换成**公开演示配置**，不能用于真实家庭设备；第二张是家长网页的历史界面示意，实际日期和操作以当前设备为准。公开网页在部分网络环境下可能无法访问；此时可用完整交付包中的 `playwise-offline.html` 导入配置文件，或在 Switch 家长区直接生成。步骤和密钥保护说明见[使用指南：在手机或电脑上生成](docs/使用指南.md#在手机或电脑上生成)。

## 主要功能

- **当天调度**：今日额度、快速加时、今日不限时、清除今日调整，以及休息期间跳过本次护眼。
- **长期计划**：每周计划、可[手动导入的地区节假日日历](docs/自制节假日日历格式.md)、指定日期额度和就寝时间。
- **自主缓冲**：可选的每日一次 5/10/15 分钟自主缓冲，以及今天、明天和规则来源说明。
- **离线加时**：Switch 本机或家长网页生成离线加时码；成功兑换一次后失效。每日上限可能使实际增加少于代码面额。
- **活动记录**：本机家庭活动记录和 7/30 天额度消耗估算。统计范围是“本机使用”，缺失日期保持未知；按游戏明细尚不可用。
- **支持与恢复**：诊断导出、紧急停用、恢复安装前设置，以及完整覆盖安装后的安全加载新版。

具体页面使用说明，参见[使用指南](docs/使用指南.md)。

<details open>
<summary>查看家长区今日调度与额度规则详情预览</summary>

![家长区今日调度预览](docs/images/usage/parent/parent-dark.png)

![今日额度规则详情预览](docs/images/usage/parent/parent-details-decision-light.png)

</details>

## 常见问题

**设置今日总额度后显示“已到限制”、还可玩 0 分钟，但游戏仍能打开？** 先确认 Nintendo 官方家长控制已开启，且没有临时解除限制；再尝试联网校准 Switch 时间，例如使用 DBI 的“工具 → NTP 时间同步”或 [QuickNTP（Tesla 时间同步工具）](https://github.com/ppkantorski/QuickNTP)。同步成功后刷新 PlayWise 状态，并用非关键游戏重新验证限制效果。校时是一个可能的解决方法，不能保证修复所有计时或限制故障。详见[使用指南的常见问题](docs/使用指南.md#常见问题)和[相关 Issue #1](https://github.com/selfuppen/NX-PlayWise/issues/1)。

## 推荐环境与验证状态

当前候选的资格目标为 Nintendo Switch OLED、HOS 22.5.0 和 Atmosphère 1.11.2。2026-08-10 的记录是早于当前 PCTL 改动的历史证据。候选默认是 `pending`；维护者已用真机验证主要功能时，可在打包命令中指定机型、HOS 和 Atmosphère，令包内 `build.json` 记录 `manual_verified`。它表示人工验机声明；只有发布 Zip 的 SHA-256 与同目录 `qualification.json` 完全匹配，才表示该原包在记录环境通过完整资格验证。打包命令见[开发环境指南](docs/开发环境指南.md#switch-完整构建)。

推荐使用 [Ultrahand Overlay](https://github.com/ppkantorski/Ultrahand-Overlay) 管理 PlayWise 游戏内浮窗。也可参考[大气层包安装与使用说明](https://docs.qq.com/doc/DVW9PVE5sU0FEd0tP)准备运行环境；相关交流群为“switch大气层超频折腾群”（QQ群 `1051287661`）。这些外部项目与社群不包含在 PlayWise 安装包中，也不代表 PlayWise 对其背书。

任我玩家长密码只保护本项目的家长区，不是 Nintendo 官方家长控制密码。PlayWise 不重置官方家长控制密码，也不解绑官方手机 App。Nintendo 官方计时按主机使用时间累计，HOME 和系统设置等亮屏使用也可能消耗额度，见[官方说明](https://support.nintendo.com/jp/switch/parentalcontrols/app/setting_change.html)。

## ROADMAP

| 功能特性 | 当前状态 | 实现时间 |
| :--- | :--- | :--- |
| 中国国家法定节假日时间设置 | 已实现，内置 2026 年日历 | 2026-08-11 |
| 主机应用暗黑模式 | 已实现三态主题；浮窗保持固定暗色 | 2026-08-13 |
| 日期计划、规则预览、每日自主缓冲 | 已实现；自主缓冲默认关闭 | 2026-08-24 |
| 家庭活动记录、7/30 天额度消耗估算 | 已实现；缺失日期标为未知 | 2026-08-24 |
| 就寝时间（bedtime） | 已实现；已提供跨夜计划、后台限制和浮窗恢复；新安装默认关闭 | 2026-09-11 |
| 浮窗儿童加时兑换与家长快捷操作 | 已实现；受限时支持浮窗兑换加时码、领取缓冲与快捷解限 | 2026-09-28 |
| 今日调度看板与决策链路即时预览 | 已实现；优化卡片分组、临时额度即时预览与决策流下钻 | 2026-09-29 |
| 全景操作音效体系 (audout 引擎) | 已实现；内置 12 档原生风格音效、拨轮音与按键音效总开关 | 2026-10-01 |
| 多语言国际化 (繁体中文 / 英文) | 已实现；支持界面与文档双语切换、系统语言解耦与动态切换 | 2026-10-02 |
| 危险操作长按充能确认与保护 | 已实现；清除调度与安全重置支持长按充能进度及连续音效反馈 | 2026-10-02 |
| 自定义快捷键录制 | 验证中；预设组合已开放，录制入口暂未发布 | 待定 |
| 按游戏时间统计 | TODO；`pdm:qry` 真机证据门禁中，当前显示不可用 | 待定 |

每日额度耗尽和就寝时间生效后，PlayWise 侧只保留浮窗作为主机内操作入口：每日限制可兑换加时码，或由家长单次授权增加分钟、设为今日不限时；就寝限制可跳过本次、关闭计划或恢复安装前设置。Nintendo 原生弹窗的官方家长控制密码临时解锁仍由 Nintendo 提供。若浮窗或后台不可用，PlayWise 没有可靠的主机内自救路径，也不会自动写入启动恢复旗标。

## 项目文档

- [使用指南](docs/使用指南.md) ([English](docs/USER_GUIDE.md))
- [开发指南](docs/开发指南.md) ([English](docs/DEVELOPER_GUIDE.md))
- [开发环境指南](docs/开发环境指南.md) ([English](docs/DEVELOPMENT_ENVIRONMENT_GUIDE.md))
- [协议](docs/协议.md) ([English](docs/PROTOCOL.md))
- [测试指南](docs/测试指南.md) ([English](docs/TESTING_GUIDE.md))
- [PCTL 集成架构](docs/PCTL集成架构.md) ([English](docs/PCTL_ARCHITECTURE.md))

## 致谢与许可

实现思路参考 [gmaitxqqq/switch-pctltcp-remoteandlocal](https://github.com/gmaitxqqq/switch-pctltcp-remoteandlocal) 和 [tailiang2008/NX-Pctl-Manager](https://github.com/tailiang2008/NX-Pctl-Manager)。项目采用 Apache License 2.0，与 Nintendo、Atmosphère、libnx 或 Ultrahand Overlay 无隶属或背书关系。

## 下载统计

[![Total Downloads](https://img.shields.io/github/downloads/selfuppen/NX-PlayWise/total?style=flat-square&color=6f42c1)](https://github.com/selfuppen/NX-PlayWise/releases)
[![Latest Release Downloads](https://img.shields.io/github/downloads/selfuppen/NX-PlayWise/latest/total?style=flat-square&color=blue)](https://github.com/selfuppen/NX-PlayWise/releases/latest)
[![playwise-complete.zip Downloads](https://img.shields.io/badge/dynamic/json?style=flat-square&color=green&label=playwise-complete.zip&query=$.assets[1].download_count&url=https://api.github.com/repos/selfuppen/NX-PlayWise/releases/latest)](https://github.com/selfuppen/NX-PlayWise/releases/latest)
[![playwise.zip Downloads](https://img.shields.io/badge/dynamic/json?style=flat-square&color=orange&label=playwise.zip&query=$.assets[0].download_count&url=https://api.github.com/repos/selfuppen/NX-PlayWise/releases/latest)](https://github.com/selfuppen/NX-PlayWise/releases/latest)
