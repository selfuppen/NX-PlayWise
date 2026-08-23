<div align="center">
  <img src="tools/ptc_frontend/playwise_logo.svg" alt="任我玩 PlayWise" width="180">

  # 任我玩 · PlayWise

  **Play Wise. Play More.**
</div>

NX-PlayWise 是项目和 GitHub 仓库名称，产品品牌为“任我玩 · PlayWise”。它是面向 Nintendo Switch 自定义固件（CFW）用户的本地游玩时间控制工具：家长可以直接在主机上设置今日总额度和每周计划，**不依赖手机 App、不需要 Nintendo Account，也不需要 Switch 连接互联网**。项目包含常驻后台服务、从 Homebrew Menu 打开的主机应用、游戏内浮窗，以及可选的家长网页。

**离线加时是 PlayWise 的核心功能：家长为指定分钟数生成当天有效的 8 位加时码，通过电话、消息或当面告知等方式交给孩子；孩子在 Switch 上兑换后即可获得相应的游玩时间。代码在本机生成和验证，不经过 PlayWise 服务器，即使没有 Wi-Fi 或互联网也能使用。**

> [!WARNING]
> **仅支持已安装自定义固件的 Nintendo Switch，推荐 Atmosphère；未破解的原厂零售主机无法使用。** PlayWise 通过常驻后台服务（sysmodule）调用受限的 Horizon PCTL 系统家长控制服务，因此必须运行在允许 Homebrew 和自定义系统组件的环境中。

> [!NOTE]
> PlayWise 不会破解账号、绕过在线验证，也不提供 Nintendo 官方家长控制 PIN 的重置、删除或官方手机 App 解绑功能。它只把本项目支持的游玩额度、系统计时、到期提醒和离线加时功能放到主机本地管理；“PlayWise PIN”仅用于保护本项目的家长区，不是 Nintendo 官方 PIN。

当前版本是已在 Nintendo Switch OLED、HOS 22.5.0 和 Atmosphère 1.11.2 环境完成测试，其它系统版本未充分测试，有风险请周知。

- 项目仓库：[selfuppen/NX-PlayWise](https://github.com/selfuppen/NX-PlayWise)
- 家长网页：优先使用完整交付包中的 `playwise-offline.html`；网络可访问时也可使用[公开演示网页](https://selfuppen.github.io/NX-PlayWise/)
- 安装与使用：[使用指南](docs/使用指南.md)

## 可以做什么

- 查看今日额度消耗估算、今日总额度和今天还可玩；
- 设置今日总额度、每周计划、中国国家节假日规则和最长 366 天的临时日期计划；
- 在家长区预览未来 7 天规则；孩子区查看今天、明天及规则来源；
- 可选开启每天一次的 5/10/15 分钟“今日自主缓冲”；
- 查看最多 200 条脱敏家庭活动记录，以及缺失日期不猜测的 7/30 天额度消耗估算；
- 由家长为指定分钟数生成当天有效的 8 位加时码；
- 在完全离线、无 Wi-Fi 的环境中，把加时码直接告诉孩子；
- 孩子在主机应用或游戏内浮窗中兑换代码，获得相应的游玩时间；
- 可选：通过二维码或配置文件将 Switch 配对到家长网页，在可信的手机或电脑上本地生成加时码；
- 在异常时导出诊断、紧急停用控制或恢复安装前状态。

## 快速开始

1. 准备已安装 Atmosphère 和 Homebrew Menu 的 Switch；如需游戏内入口，另行安装 Ultrahand Overlay。
2. 首次使用优先下载 `playwise-complete-<版本>.zip` 完整交付包；其中的 `playwise-<版本>.zip` 用于安装 Switch 端，`playwise-offline.html` 用于家长手机或电脑。
3. 从 Homebrew Menu 打开“任我玩”，完成首次设置。
4. 在家长区设置今日总额度、周计划或国家节假日规则；需要临时加时时，由家长生成指定分钟数的 8 位码并告诉孩子。
5. 孩子无需联网，在主机应用或游戏内浮窗中输入代码，确认后即可获得相应的游玩时间。

完整步骤、升级方法和截图位置见[使用指南](docs/使用指南.md)。

## 离线操作演示

安装配置后，日常生成、传递和兑换加时码均可在无网络环境中进行。

### 小孩操作

**浮窗兑换加时码** - 已经被限制时，利用 Tesla 插件兑换加时码：

![游戏内浮窗兑换加时码确认示意](docs/images/usage/10-2-overlay-verified-confirm.jpg)
   
**小孩区** - 没到时间时，兑换加时码：

![小孩区浅色主题](docs/images/usage/02-launch-playwise-light.jpg)

### 家长操作

**家长生成加时码网页** - 存储在本地的 HTML 文件，无需联网，手机电脑都支持：

![家长网页-生成加时码](docs/images/usage/11-parent-pwa.jpg)

**家长区** - 应用里的控制设置：

![家长区首页](docs/images/usage/04-parent-home.jpg)

## 家长网页

家长端网页在浏览器本地生成加时码，不会把设备 ID、密钥或代码提交给业务后端。普通用户优先使用完整交付包内的 `playwise-offline.html`：无需安装应用、Python、前端工具或本地服务器，准备完成后日常生成不需要访问互联网；孩子在 Switch 上兑换时同样不需要联网。

公开演示网页托管在 GitHub Pages，中国大陆网络环境下可能无法访问或加载不稳定，因此只作为网络可访问时的便捷方式。需要手机扫码自动配对或把网页安装到桌面时，可使用公开页面或把完整网页部署到可信的 HTTPS 静态站点。具体操作见[使用指南的家长网页章节](docs/使用指南.md#家长网页)。

## 推荐环境

当前版本已在 Nintendo Switch OLED、HOS 22.5.0 和 Atmosphère 1.11.2 环境完成资格验证（2026-08-10）。

推荐使用 [Ultrahand Overlay](https://github.com/ppkantorski/Ultrahand-Overlay) 管理 PlayWise 游戏内浮窗。也可参考[大气层包安装与使用说明](https://docs.qq.com/doc/DVW9PVE5sU0FEd0tP)准备运行环境(整合包和大气层使用交流QQ群：switch大气层超频折腾群 ，群号：`1051287661`)。

这些外部项目不包含在 PlayWise 安装包中, 跟项目本身没有任何直接关系。

## ROADMAP

| 功能特性 | 状态 |
| :--- | :--- |
| 中国国家法定节假日时间设置 | 已实现（内置 2026 年日历） |
| 日期计划、规则预览与每日自主缓冲 | 已实现（默认关闭） |
| 家庭活动记录与 7/30 天额度消耗估算 | 已实现（本机使用；缺失数据标为未知） |
| Companion NRO 暗黑模式 | 已实现（三态主题；Overlay 保持固定暗色） |
| 自定义快捷键录制 | 验证中（预设组合已开放；真机验证后发布录制入口） |
| bedtime | 真机证据门禁中（标准版未开放） |
| 按游戏时间统计 | `pdm:qry` 真机证据门禁中（当前显示不可用） |

## 更多文档

- [开发指南](docs/开发指南.md)
- [开发环境指南](docs/开发环境指南.md)
- [协议](docs/协议.md)
- [测试指南](docs/测试指南.md)
- [PCTL 集成架构](docs/PCTL集成架构.md)

## 致谢

PlayWise 的实现思路参考并感谢以下项目：

- [gmaitxqqq/switch-pctltcp-remoteandlocal](https://github.com/gmaitxqqq/switch-pctltcp-remoteandlocal)
- [tailiang2008/NX-Pctl-Manager](https://github.com/tailiang2008/NX-Pctl-Manager)

## License

Apache License 2.0。本项目与 Nintendo、Atmosphère、libnx 或 Ultrahand Overlay 无隶属或背书关系。
