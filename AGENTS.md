# Repository Guidelines

## 项目结构与模块组织

本仓库是 Nintendo Switch 游玩时间控制项目的本地验证版本，优先保证协议、队列和控制策略可在主机侧测试。

- `common/`：跨端协议常量与平台无关 C 核心代码，例如 `common/protocol/*.h`、`common/token/*.h`。
- `tools/`：Python 开发工具与参考实现，包括令牌生成、协议探测、请求队列、前端服务和统一打包入口。
- `tests/`：主机侧 C/Python/前端测试与 fixtures，覆盖离线加时令牌、请求流、安全状态机、UI 和 package 门禁。
- `docs/`：架构、协议、开发环境、测试与真实设备验证文档。开始开发前先读根目录 `README.md` 和 `docs/开发环境指南.md`。

维护者机器在仓库相对路径 `../libnx` 提供可选的上游 libnx checkout。涉及 PCTL service/session、公开命令签名或 libnx dispatch 行为时可查阅该目录；实际构建使用 devkitPro 环境中的 libnx。当前调查用源码的 `nx/include/switch/services/pctl.h` 与 `nx/source/services/pctl.c` 不包含 `StartPlayTimer (1451)`、`GetPlayTimerRemainingTime (1454)`、`GetPlayTimerSettings (145601)` 或 `SetPlayTimerSettingsForDebug (195101)` 的公开封装，因此不得用 libnx 缺失的定义推断这些私有命令的参数单位或 0x44 raw layout；相关布局必须以真机 A/B 证据和仓库协议文档为准。

维护者机器在仓库相对路径 `../libtesla` 提供可选的上游 libtesla checkout。涉及 Overlay 生命周期、输入处理、绘制或 libtesla API 行为时可查阅该目录。项目构建必须使用仓库内 `companion/overlay/vendor/libtesla/` 固定的版本，不得依赖工作区外的路径；同步 vendored 文件时同时更新 `companion/overlay/vendor/libtesla/UPSTREAM.txt` 中的上游 commit。

维护者机器在仓库相对路径 `../Atmosphere` 提供可选的 Atmosphère checkout。只有问题涉及 `boot2.flag` 与 `atmosphere/contents` 扫描、sysmodule 自启动、SM/PM 服务和进程行为、hbmenu `override_config.ini` 解析、creport/fatal 取证或 Atmosphère/HOS 资格基线变化时，才把它作为 CFW 环境层参考；它不参与 PlayWise 构建，也不是 libnx、libtesla 或 Horizon PCTL 私有协议的替代来源。调查前必须用 `git -C ../Atmosphere rev-parse HEAD`、`git -C ../Atmosphere describe --tags --always --dirty` 和 `git -C ../Atmosphere status --short` 记录版本与工作树状态，只把绑定到该 commit 的已跟踪源码作为证据。未跟踪文件、当前 checkout 和远端 `main`/`master` 都不得自动视为项目固定依赖或其他版本的行为证明。详细阅读决策见 `docs/开发指南.md`，PCTL 边界见 `docs/PCTL集成架构.md`。

## 上游源码与 GitHub 调研

编码代理不得把搜索 GitHub 当作普通代码修改的默认步骤。调查顺序应为：当前仓库和协议文档、仓库内 vendored 固定版本、本机已拉取的对应上游源码，最后才是 GitHub。

只有证据无法从本地获得时，或任务本身要求核对上游状态时，才必须查询 GitHub，例如：确认固定 commit、tag 或 release 的内容；调查已知 bug、安全公告、issue 或 PR；核对依赖的 breaking changes、迁移说明、许可证或 NOTICE；比较本地 vendored 修改与上游原版；或者用户明确要求调查 GitHub 仓库、issue、PR 或最新状态。

查询 GitHub 时必须记录仓库、commit/tag 或 issue/PR 链接以及查询日期。不得将 `main`/`master` 当前代码直接视为项目 vendored commit 的行为，也不得因远端有更新就在未经请求和验证的情况下升级依赖。本地固定版本足以回答的问题不应联网搜索。

## 构建、测试与开发命令

Host Python 回归入口跨平台可用；C host、Companion NRO 和 package 的权威验证通过统一的 devkitPro 构建入口执行。维护者默认环境为 Windows、PowerShell、Docker Desktop 和 OpenSSH，其他开发者可使用满足同一门禁的等价环境。

- `python tools/test.py`：运行全部本地 Python、协议和安全打包回归。
- `python tools/package_remote.py`：在 devkitPro 环境运行 C/Python/UI 测试，清理并重新生成、校验标准分发与 Device Lab 产物；产物直接位于挂载工作区的 `build/`。
- `python tools/make_fixtures.py`：重新生成确定性的令牌 fixture。
- `python tools/grant_code.py --minutes 30 --device kid-switch --secret replace-with-long-random-secret --day-index 2380 --nonce 4660`：生成离线加时代码示例。
- `python tools/protocol_probe.py init --root <tmp-dir> --device <id> --secret <secret>`：初始化本地协议目录用于手工探测。

维护者默认 devkitPro profile 通过 `root@127.0.0.1:1888` 访问，仓库挂载为 `/ws/playwise`；host、port、user、容器路径和私钥均可通过脚本参数覆盖。密码只允许由 OpenSSH 交互读取，也可使用已授权私钥。容器脚本使用一次 SSH 会话完成清理、测试和构建；Zip 直接留在挂载工作区，不要维护手工 SSH、`docker exec` 或复制回本地的旁路构建命令。完整说明见 `docs/开发环境指南.md`。

容器直接读取挂载的当前工作区，因此未提交改动也会参与构建，不需要先提交或推送。除非用户明确只要求本地 Python 快速回归，否则涉及 C、Makefile、NRO 或 package 的最终验证都必须走该入口。

## 编码风格与命名约定

Python 代码使用 4 空格缩进、类型注解和 `from __future__ import annotations`。保持工具脚本无第三方依赖，便于在早期主机测试中直接运行。文件、函数和变量使用 `snake_case`；协议常量使用全大写，例如 `TOKEN_VERSION`。C 头文件保持小型、稳定、无平台依赖，`common` 不应引用 libnx、SD 卡路径、UI、真实时钟或进程级可变状态。

## 注释与文档要求

新增复杂协议逻辑时，在代码附近添加简短注释说明位布局、错误原因或安全边界；不要为显而易见的赋值写注释。对外可见的行为变更应同步更新 `docs/协议.md`、`docs/测试指南.md` 或相关路线图。错误码、请求类型、fixture 字段和稳定性规则必须有文档来源，避免只存在于测试或实现中。

## 测试指南

测试以主机侧脚本为主，当前不依赖 pytest。新增测试文件建议命名为 `test_<feature>.py`，放在对应阶段目录下。测试必须可重复，不依赖真实时间、真实 Switch 服务或手写随机 fixture。涉及令牌、请求队列、nonce 消耗、PCTL 写入路径或错误码的改动，至少补充一个成功路径和一个失败路径。

## 提交与 Pull Request 指南

历史提交主要使用简洁主题行，包含 `feat(scope): ...`、`docs(scope): ...`、`doc: ...` 等形式，也接受 `Initial commit` 这类基础提交。建议提交信息使用现在时，说明用户可观察的变化，例如 `feat(observe): validate offline code requests`。PR 应包含变更摘要、已运行的测试命令、风险点；涉及 UI、协议文件或真实设备流程时，附截图、样例 JSON 或设备验证记录。

在 Windows PowerShell 中创建包含中文的 commit message 时，必须使用无 BOM 的 UTF-8 message 文件再执行 `git commit -F <file>`；不要使用会写入 BOM 的 `Set-Content -Encoding utf8` 生成提交信息文件。可使用 `[System.Text.UTF8Encoding]::new($false)` 和 `[System.IO.File]::WriteAllText(...)` 写入。提交后必须运行 `git log -1 --pretty=%B` 验证标题前没有不可见 BOM；如果发现标题前有异常隐藏字符，立即用无 BOM message 文件 `git commit --amend -F <file>` 修正。

## 安全与配置提示

标准分发构建不提供运行时控制模式，并包含用于常驻启动的 `boot2.flag`；Device Lab 包默认不包含该 flag。`disable.flag` 阻止新的控制写入，但不阻止状态、诊断和恢复。无效 token、PCTL 写失败、result/ledger 失败和回滚路径不得消耗 nonce。不要在 child-visible UI、日志、fixture 之外的示例中泄露真实 `grant_secret`。

## Agent 专用说明

修改前先查看 `docs/开发指南.md`、`docs/协议.md` 和相关测试。优先保持变更小而可验证；不要重写无关文档或回退用户改动。新增行为时同时补测试和必要文档，并在最终回复中列出实际执行过的命令。

本仓库中文文档使用 UTF-8。PowerShell 默认编码或控制台显示可能把中文读成乱码；读取或写入 `AGENTS.md`、`docs/*.md` 等中文文档时，应显式使用 UTF-8，例如 `Get-Content <file> -Encoding utf8`，避免误判编码或把文档写坏。

涉及 C 编译或 package 验证时运行 `python tools/package_remote.py`；不要绕过其中的构建产物清理、C/Python/UI 测试和 Zip 校验。Host 缺少本地 `make` 或 C 编译器时，仍须在满足项目契约的 devkitPro 环境完成验证。
