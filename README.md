<div align="center">
  <img src="tools/ptc_frontend/playwise_logo.svg" alt="任我玩 PlayWise" width="180">

  # 任我玩 · PlayWise

  **Play Wise. Play More.**
</div>

NX-PlayWise 是项目和 GitHub 仓库名称，产品品牌为“任我玩 · PlayWise”。PlayWise 是面向 Nintendo Switch 自制系统环境的本地游玩额度管理工具，由常驻 sysmodule、Companion NRO、Overlay 和家长端离线网页组成，提供状态查询、8 位加时码、今日额度、周计划和可恢复的自动应用。项目当前处于 `0.1.3-alpha` 开发验证阶段，尚未公开发布。

- 项目仓库：[selfuppen/NX-PlayWise](https://github.com/selfuppen/NX-PlayWise)
- 家长端 PWA：[公开演示网页](https://selfuppen.github.io/NX-PlayWise/)

> [!IMPORTANT]
> 当前产品边界是“额度管理、系统计时和到期提醒”，不承诺到期强制锁屏。`raw_block`、`suspend`、私有布局探针和故障注入只存在于独立 Device Lab，不进入标准分发包。

## 产品边界

项目采用三层结构：

- 标准分发构建：简洁的日常额度管理界面；
- 分发构建内建 Support：只读诊断、紧急停用、事务恢复和安装快照恢复；
- 内部 Device Lab：危险探针和强制实验能力，使用独立 Title ID、IPC service 和 SD 根目录，不公开分发。

当前实现支持：

- 查询今日已玩、总额度和剩余时间；
- 输入绑定设备、当天有效且仅成功使用一次的 8 位加时码，先预览生效结果再确认兑换；
- 设置今日总额度、增加分钟、今日不限时、恢复周计划；
- 配置星期日至星期六的限时或不限时计划；
- 在周计划页面直接编辑七天规则，并在离开未保存草稿前确认；
- 通过二维码或导出文件将设备安全配对到家长端离线网页；
- 自动应用规则，并在 PCTL 状态传播期间显示“正在同步”；
- 导出不含 secret、PIN、离线码和完整 nonce 的诊断信息。

## 使用家长端 PWA

在 Switch 的“加时码与安全 → 加时码生成”中验证 PlayWise PIN，然后用可信的家长设备扫描二维码；也可把 `sdmc:/switch/playwise/parent-import.json` 导入[公开演示网页](https://selfuppen.github.io/NX-PlayWise/)。网页绑定设备后，选择 UTC+8 生效日期和分钟数，即可生成交给孩子输入的 8 位加时码。建议收藏配对后打开的页面地址，并妥善保护包含配对权限的二维码或导入文件。

加时码使用浏览器 Web Crypto 在本地生成，本项目不会把设备 ID、密钥、日期或生成出的代码提交给业务后端。设备 ID、密钥、常用时长和 nonce 历史会以明文保存在 `selfuppen.github.io` origin 的 `localStorage`，因此只应在可信的家长设备上使用。页面首次加载或更新需要联网获取静态资源；缓存完成后可以离线生成。更多安全说明见[使用指南](docs/使用指南.md)。

标准分发 profile 只提供[协议](docs/协议.md)列出的请求类型，不提供 bedtime、`limit_action`、家长临时解锁、禁玩日、`capabilities.json` 或运行时控制模式。

## Overlay 与推荐运行环境

Companion NRO 是日常使用的主入口：孩子可在其中输入加时码，家长验证 PlayWise PIN 后可进入家长区完成周计划、今日额度、配对密钥、诊断和恢复等完整管理。

PlayWise Overlay 则是游戏内和受限状态下的加时入口：打开后可查看今天已玩、总额度和剩余时间，也可输入、预览并兑换 8 位加时码。设置两个看似重复的加时码入口，最重要的原因是家长控制限制生效时 Companion NRO 可能无法打开，此时仍可通过 Overlay 紧急输入加时码、恢复可玩时间。Overlay 只向 PlayWise sysmodule 提交请求，不直接访问或修改 PCTL，也不替代 Companion NRO 的完整管理功能。

推荐使用 [Ultrahand Overlay](https://github.com/ppkantorski/Ultrahand-Overlay) 管理和打开 PlayWise Overlay，并参考[大气层包安装与使用说明](https://docs.qq.com/doc/DVW9PVE5sU0FEd0tP)准备运行环境；相关使用交流 QQ 群为 `1051287661`。Ultrahand Overlay 和大气层整合包是外部项目，不包含在 PlayWise 安装包中。PlayWise 的 Overlay 组件只安装 `sdmc:/switch/.overlays/pctc.ovl`，不会写入或修改 Overlay 管理器的全局配置。

Ultrahand 项目和整合包说明于 2026-08-10 核对；它们是推荐的外部运行环境，不是 PlayWise 的固定构建依赖。

## 兼容与安全

目标验证基线是 Nintendo Switch OLED、HOS 22.5.0 和 Atmosphère 1.11.2。当前 `0.1.3-alpha` 候选构建尚未完成该组合的真机资格验证。

启动时先做只读预检：

- 已验证组合进入等待家长确认；
- 结构正常但环境未认证时显示“兼容性待确认”，家长可用 PIN 和长按确认；
- snapshot、PCTL layout、启动遗留事务、PCTL 只读初始化或 release manifest 失败时进入“保护模式”，标准分发构建不允许绕过。

首次确认后才保存安装快照、解除当前限制、等待 5 秒同步宽限并进入 Active/Enforce。普通写入先持久化恢复事务；写后最多确认 30 秒，未确认则回滚，无法证明恢复时进入保护并创建 `disable.flag`。该 flag 只阻止新的控制写入，状态查询、诊断导出和恢复仍可使用。

## 构建与测试

本地 Python、协议和安全打包回归：

```text
python tools/test.py
```

涉及 C、Makefile、NRO、Overlay、sysmodule 或 package 时，使用唯一权威入口：

```text
python tools/package_remote.py
```

成功后只生成：

```text
build/packages/playwise-0.1.3-alpha.zip
```

候选包必须包含 sysmodule、Companion NRO、Overlay、`boot2.flag` 和运行时 `build.json`。构建门禁使用外部生成的 release manifest 校验包内 `build.json`、组件嵌入信息和版本一致性，并拒绝多个候选 Zip、LAB handler、运行时控制模式、占位 secret 或 manifest 不一致。

内部 Device Lab 只在 devkitPro 环境显式构建：

```sh
make device-lab-package
```

该目标不属于 `make packages`，产物位于 `build/device-lab/`，默认不含 `boot2.flag`。

## 文档

- [使用指南](docs/使用指南.md)
- [开发指南](docs/开发指南.md)
- [开发环境指南](docs/开发环境指南.md)
- [协议](docs/协议.md)
- [测试指南](docs/测试指南.md)
- [PCTL 集成架构](docs/PCTL集成架构.md)

## License

Apache License 2.0。本项目与 Nintendo、Atmosphère、libnx 或 Ultrahand Overlay 无隶属或背书关系。
