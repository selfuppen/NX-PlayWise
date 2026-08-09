<div align="center">
  <img src="tools/ptc_frontend/playwise_logo.svg" alt="任我玩 PlayWise" width="180">

  # 任我玩 · PlayWise

  **Play Wise. Play More.**
</div>

PlayWise 是面向 Nintendo Switch 自制系统环境的本地游玩额度管理工具。公开 Release 由常驻 sysmodule、Companion NRO、Tesla Overlay 和家长端离线网页组成，提供状态查询、8 位加时码、今日额度、周计划和可恢复的自动应用。

> [!IMPORTANT]
> 稳定版承诺“额度管理、系统计时和到期提醒”，不承诺到期强制锁屏。`raw_block`、`suspend`、私有布局探针和故障注入只存在于独立 Device Lab，不进入公开包。

## 产品边界

项目采用三层结构：

- 唯一公开 Release：简洁的日常额度管理界面；
- Release 内建 Support：只读诊断、紧急停用、事务恢复和安装快照恢复；
- 内部 Device Lab：危险探针和强制实验能力，使用独立 Title ID、IPC service 和 SD 根目录，不公开分发。

Release 支持：

- 查询今日已玩、总额度和剩余时间；
- 输入绑定设备、当天有效且仅成功使用一次的 8 位加时码，先预览生效结果再确认兑换；
- 设置今日总额度、增加分钟、今日不限时、恢复周计划；
- 配置星期日至星期六的限时或不限时计划；
- 在周计划页面直接编辑七天规则，并在离开未保存草稿前确认；
- 通过二维码或导出文件将设备安全配对到家长端离线网页；
- 自动应用规则，并在 PCTL 状态传播期间显示“正在同步”；
- 导出不含 secret、PIN、离线码和完整 nonce 的诊断信息。

Release 不包含 bedtime、`limit_action`、家长临时解锁、禁玩日、旧请求 15–18、`capabilities.json` 或运行时控制模式。

## 兼容与安全

已声明的验证基线是 Nintendo Switch OLED、HOS 22.5.0；资格测试使用 Atmosphère 1.11.2。当前 `0.1.2-alpha` 最终产物仍需在该组合上重新完成资格验证，历史包结果不能继承。

启动时先做只读预检：

- 已验证组合进入等待家长确认；
- 结构正常但环境未认证时显示“兼容性待确认”，家长可用 PIN 和长按确认；
- snapshot、PCTL layout、旧事务、PCTL 只读初始化或 release manifest 失败时进入“保护模式”，Release 不允许绕过。

首次确认后才保存安装快照、解除当前限制、等待 5 秒同步宽限并进入 Active/Enforce。普通写入先持久化恢复事务；写后最多确认 30 秒，未确认则回滚，无法证明恢复时进入保护并创建 `disable.flag`。该 flag 只阻止新的控制写入，状态查询、诊断导出和恢复仍可使用。

## 构建与测试

本地 Python、协议和安全打包回归：

```powershell
python .\tools\test.py
```

涉及 C、Makefile、NRO、Overlay、sysmodule 或 package 时，使用唯一权威入口：

```powershell
python .\tools\package_remote.py
```

成功后只生成：

```text
build/packages/playwise-0.1.2-alpha.zip
```

包必须包含 sysmodule、Companion NRO、Overlay、`boot2.flag` 和一致的 release manifest。构建门禁会拒绝多个公开 Zip、LAB handler、旧模式、占位 secret、组件版本或 manifest 不一致。

内部 Device Lab 只在 devkitPro 环境显式构建：

```sh
make device-lab-package
```

该目标不属于 `make packages`，产物位于 `build/device-lab/`，默认不含 `boot2.flag`。

## 文档

- [使用指南](docs/使用指南.md)
- [开发指南](docs/开发指南.md)
- [协议](docs/协议.md)
- [测试指南](docs/测试指南.md)
- [PCTL 集成架构](docs/PCTL集成架构.md)

## License

Apache License 2.0。本项目与 Nintendo、Atmosphère、libnx、Tesla Menu 或 nx-ovlloader 无隶属或背书关系。
