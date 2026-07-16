# Repository Guidelines

## 项目结构与模块组织

本仓库是 Nintendo Switch 游玩时间控制项目的本地验证版本，优先保证协议、队列和控制策略可在主机侧测试。

- `common/`：跨端协议常量与未来 C 核心代码，例如 `common/protocol/*.h`、`common/token/*.h`。
- `tools/`：Python 开发工具与参考实现，包括令牌生成、协议探测、请求队列和 observe 处理器。
- `tests/`：主机侧测试脚本与 fixtures。`tests/mvp/` 覆盖离线加时令牌，`tests/observe/` 覆盖 observe 请求流。
- `docs/`：架构、协议、测试与真实设备验证文档。开始开发前先读根目录 `README.md`。

## 构建、测试与开发命令

当前已有 `Makefile`，但 Windows 本地通常只要求 Python 快速回归；C host、Companion NRO 和 package 的权威验证默认通过远程 devkitPro 容器执行。

- `python tools/make_fixtures.py`：重新生成确定性的令牌 fixture。
- `python tests/mvp/test_token_v1.py`：运行 MVP 令牌编码、解码和 CLI 一致性测试。
- `python tests/observe/test_observe_queue.py`：运行 observe 请求队列和 dry-run 行为测试。
- `python tools/grant_code.py --minutes 30 --device kid-switch --secret replace-with-long-random-secret --day-index 2380 --nonce 4660`：生成离线加时代码示例。
- `python tools/protocol_probe.py init --root <tmp-dir> --device <id> --secret <secret>`：初始化本地协议目录用于手工探测。

远程宿主机通过 SSH 别名 `renqi-nintendo-switch-dev`（`ygq@5.78.109.249:22`）访问，宿主机项目路径为 `/home/ygq/nintendo/switch-play-time-control-local`。devkitPro 环境位于宿主机 Docker 容器 `devkitpro-ssh-v1`，同一仓库在容器内挂载为 `/ws/switch-play-time-control-local`，使用 `master` 分支开发。SSH 使用密码认证时由 OpenSSH 交互提示输入宿主机密码，不要把密码写入仓库、脚本或命令行参数。需要验证 C host、NRO 或 package 时，不要因为本机缺少 `make`、`gcc` 或 WSL 发行版就判定“无法执行”；应先在本地通过 Python 快速回归、提交并推送 `master`，再在宿主机仓库快进拉取，最后通过 `docker exec` 运行容器内构建测试：

- `ssh renqi-nintendo-switch-dev "git -C /home/ygq/nintendo/switch-play-time-control-local pull --ff-only origin master && docker exec devkitpro-ssh-v1 sh -lc 'cd /ws/switch-play-time-control-local && make'"`：登录宿主机并拉取最新 `master`，再在容器内编译并运行 C host 测试和 Python 回归。

当前仓库已有 Makefile，远程完整验证应从宿主机通过 `docker exec` 在 devkitPro 容器内执行。非交互容器会话需要显式设置 devkitPro 环境变量：

- `ssh renqi-nintendo-switch-dev "git -C /home/ygq/nintendo/switch-play-time-control-local pull --ff-only origin master && docker exec -e DEVKITPRO=/opt/devkitpro -e DEVKITARM=/opt/devkitpro/devkitARM -e DEVKITA64=/opt/devkitpro/devkitA64 -e PATH=/opt/devkitpro/devkitA64/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin devkitpro-ssh-v1 sh -lc 'cd /ws/switch-play-time-control-local && make && make companion-nro && make package-safe package-observe package-safe-nro'"`：登录宿主机并拉取代码，再执行容器内 host 测试、Companion NRO 构建和当前 package 验证。

如果本地存在未提交改动，远程不会看到这些改动；必须先提交并推送后再跑远程验证。除非用户明确只要求本地 Python 快速回归，否则涉及 C 代码、Makefile、NRO 或 package 的最终验证都应走上述 SSH 远程路径。

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

默认控制模式必须保持 `observe`，默认包不应包含 `boot2.flag`。`disable.flag` 优先于所有控制模式。无效 token、dry-run、PCTL 写失败和备份失败路径不得消耗 nonce。不要在 child-visible UI、日志、fixture 之外的示例中泄露真实 `grant_secret`。

## Agent 专用说明

修改前先查看 `docs/开发指南.md`、`docs/协议.md` 和相关测试。优先保持变更小而可验证；不要重写无关文档或回退用户改动。新增行为时同时补测试和必要文档，并在最终回复中列出实际执行过的命令。

本仓库中文文档使用 UTF-8。PowerShell 默认编码或控制台显示可能把中文读成乱码；读取或写入 `AGENTS.md`、`docs/*.md` 等中文文档时，应显式使用 UTF-8，例如 `Get-Content <file> -Encoding utf8`，避免误判编码或把文档写坏。

涉及远程编译或容器验证时，使用宿主机 `renqi-nintendo-switch-dev`、宿主机路径 `/home/ygq/nintendo/switch-play-time-control-local`、容器 `devkitpro-ssh-v1` 和容器路径 `/ws/switch-play-time-control-local`；不要假设远程已自动同步本地工作区，必须通过本地提交、推送，在宿主机仓库执行 `git pull --ff-only origin master`，再用 `docker exec` 在容器内测试。本机缺少 `make`、`gcc` 或可用 WSL 发行版不是跳过 C host 验证的理由；应改走远程 devkitPro 容器。
