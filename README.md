<div align="center">
  <img src="tools/ptc_frontend/playwise_logo.svg" alt="任我玩 PlayWise" width="180">

  # 任我玩 · PlayWise

  **Play Wise. Play More.**
</div>

NX-PlayWise 是项目和 GitHub 仓库名称，产品品牌为“任我玩 · PlayWise”。它是在 Nintendo Switch 自制系统环境中运行的本地游玩额度管理工具，包含常驻 sysmodule、Companion NRO、Overlay 和家长端 PWA。

当前公开版本为 `0.1.3`，已在 Nintendo Switch OLED、HOS 22.5.0 和 Atmosphère 1.11.2 环境完成资格验证。

- 项目仓库：[selfuppen/NX-PlayWise](https://github.com/selfuppen/NX-PlayWise)
- 家长端 PWA：[公开演示网页](https://selfuppen.github.io/NX-PlayWise/)
- 安装与使用：[使用指南](docs/使用指南.md)

> [!IMPORTANT]
> PlayWise 利用系统家长控制功能完成游玩额度管理、系统计时和到期提醒。

## 可以做什么

- 查看今天已玩、总额度和剩余时间；
- 设置今日额度和每周计划；
- 由家长生成当天有效的 8 位加时码；
- 在 Companion NRO 或游戏内 Overlay 中兑换加时码；
- 通过二维码或配置文件将 Switch 配对到家长端 PWA；
- 在异常时导出诊断、紧急停用控制或恢复安装前状态。

## 快速开始

1. 准备已安装 Atmosphère 和 Homebrew Menu 的 Switch；如需游戏内入口，另行安装 Ultrahand Overlay。
2. 下载 PlayWise 安装包，把内容安装到 SD 卡根目录。
3. 从 Homebrew Menu 打开“任我玩”，完成首次设置。
4. 在家长区设置今日额度或周计划；需要临时加时时，生成 8 位码并在 NRO 或 Overlay 中兑换。

完整步骤、升级方法和截图位置见[使用指南](docs/使用指南.md)。

## 家长端 PWA

家长端网页在浏览器本地生成加时码，不会把设备 ID、密钥或代码提交给业务后端。公开演示网页托管在 GitHub Pages，中国大陆网络环境下可能无法访问或加载不稳定，可能需要使用能够访问 GitHub Pages 的网络环境。

无法稳定访问时，可以在电脑上本地运行 PWA，再导入 Switch 导出的 `parent-import.json`；需要手机扫码使用时，则应把完整前端部署到可信的 HTTPS 静态站点。具体操作见[使用指南的家长端 PWA 章节](docs/使用指南.md#家长端-pwa)。

## 推荐环境

`0.1.3` 已在 Nintendo Switch OLED、HOS 22.5.0 和 Atmosphère 1.11.2 环境完成资格验证（2026-08-10）。

推荐使用 [Ultrahand Overlay](https://github.com/ppkantorski/Ultrahand-Overlay) 管理 PlayWise Overlay。也可参考[大气层包安装与使用说明](https://docs.qq.com/doc/DVW9PVE5sU0FEd0tP)准备运行环境(整合包和大气层使用交流QQ群：switch大气层超频折腾群 ，群号：`1051287661`)。

这些外部项目不包含在 PlayWise 安装包中, 跟项目本身没有任何直接关系。

## 更多文档

- [开发指南](docs/开发指南.md)
- [开发环境指南](docs/开发环境指南.md)
- [协议](docs/协议.md)
- [测试指南](docs/测试指南.md)
- [PCTL 集成架构](docs/PCTL集成架构.md)

## License

Apache License 2.0。本项目与 Nintendo、Atmosphère、libnx 或 Ultrahand Overlay 无隶属或背书关系。
