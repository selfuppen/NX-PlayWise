# Repository Guidelines

## 项目结构与模块组织

本仓库是 Nintendo Switch 游玩时间控制项目的本地验证版本，优先保证协议、队列和控制策略可在主机侧测试。

- `common/`：跨端协议常量与平台无关 C 核心代码，例如 `common/protocol/*.h`、`common/token/*.h`。
- `tools/`：Python 开发工具与参考实现，包括令牌生成、协议探测、请求队列、前端服务和统一打包入口。
- `tests/`：主机侧 C/Python/前端测试与 fixtures，覆盖离线加时令牌、请求流、安全状态机、UI 和 package 门禁。
- `docs/`：按使用、开发、设计与调研分类，统一入口为 `docs/README.md`。开始开发前先读根目录 `README.md` 和 `docs/开发/开发环境指南.md`。

维护者机器在仓库相对路径 `../libnx` 提供可选的上游 libnx checkout。涉及 PCTL service/session、公开命令签名或 libnx dispatch 行为时可查阅该目录；实际构建使用 devkitPro 环境中的 libnx。当前调查用源码的 `nx/include/switch/services/pctl.h` 与 `nx/source/services/pctl.c` 不包含 `StartPlayTimer (1451)`、`GetPlayTimerRemainingTime (1454)`、`GetPlayTimerSettings (145601)` 或 `SetPlayTimerSettingsForDebug (195101)` 的公开封装，因此不得用 libnx 缺失的定义推断这些私有命令的参数单位或 0x44 raw layout；相关布局必须以真机 A/B 证据和仓库协议文档为准。

维护者机器在仓库相对路径 `../libtesla` 提供可选的上游 libtesla checkout。涉及 Overlay 生命周期、输入处理、绘制或 libtesla API 行为时可查阅该目录。项目构建必须使用仓库内 `companion/overlay/vendor/libtesla/` 固定的版本，不得依赖工作区外的路径；同步 vendored 文件时同时更新 `companion/overlay/vendor/libtesla/UPSTREAM.txt` 中的上游 commit。

## 上游源码与 GitHub 调研

编码代理不得把搜索 GitHub 当作普通代码修改的默认步骤。调查顺序应为：当前仓库和协议文档、仓库内 vendored 固定版本、本机已拉取的对应上游源码，最后才是 GitHub。

只有证据无法从本地获得时，或任务本身要求核对上游状态时，才必须查询 GitHub，例如：确认固定 commit、tag 或 release 的内容；调查已知 bug、安全公告、issue 或 PR；核对依赖的 breaking changes、迁移说明、许可证或 NOTICE；比较本地 vendored 修改与上游原版；或者用户明确要求调查 GitHub 仓库、issue、PR 或最新状态。

查询 GitHub 时必须记录仓库、commit/tag 或 issue/PR 链接以及查询日期。不得将 `main`/`master` 当前代码直接视为项目 vendored commit 的行为，也不得因远端有更新就在未经请求和验证的情况下升级依赖。本地固定版本足以回答的问题不应联网搜索。

## 构建、测试与开发命令

Host Python 回归入口跨平台可用；C host、Companion NRO 和 package 的权威验证通过统一的 devkitPro 构建入口执行。维护者默认环境为 Windows、PowerShell、Docker Desktop 和 OpenSSH，其他开发者可使用满足同一门禁的等价环境。

- `python tools/test.py`：运行全部本地 Python、协议和安全打包回归。
- `python tools/package_remote.py`：在 devkitPro 环境运行 C/Python 核心测试，增量生成并校验标准分发包与 Eden 模拟器测试应用（产物位于 `build/eden-test/pctc-eden.nro`）；发布前或需完整交付包、Device Lab 及文档 UI 截图时使用 `--release`（或追加 `--clean` 完整清理）。
- `python tools/make_fixtures.py`：重新生成确定性的令牌 fixture。
- `python tools/grant_code.py --minutes 30 --device kid-switch --secret replace-with-long-random-secret --day-index 2380 --nonce 4660`：生成离线加时代码示例。
- `python tools/protocol_probe.py init --root <tmp-dir> --device <id> --secret <secret>`：初始化本地协议目录用于手工探测。

维护者默认 devkitPro profile 通过 `root@127.0.0.1:1888` 访问，仓库挂载为 `/ws/playwise`；host、port、user、容器路径和私钥均可通过脚本参数覆盖。密码只允许由 OpenSSH 交互读取，也可使用已授权私钥。容器脚本使用一次 SSH 会话完成清理、测试和构建；Zip 直接留在挂载工作区，不要维护手工 SSH、`docker exec` 或复制回本地的旁路构建命令。完整说明见 `docs/开发/开发环境指南.md`。

容器直接读取挂载的当前工作区，因此未提交改动也会参与构建，不需要先提交或推送。除非用户明确只要求本地 Python 快速回归，否则涉及 C、Makefile、NRO 或 package 的最终验证都必须走该入口。

## 编码风格与命名约定

Python 代码使用 4 空格缩进、类型注解和 `from __future__ import annotations`。保持工具脚本无第三方依赖，便于在早期主机测试中直接运行。文件、函数和变量使用 `snake_case`；协议常量使用全大写，例如 `TOKEN_VERSION`。C 头文件保持小型、稳定、无平台依赖，`common` 不应引用 libnx、SD 卡路径、UI、真实时钟或进程级可变状态。

## 注释与文档要求

新增复杂协议逻辑时，在代码附近添加简短注释说明位布局、错误原因或安全边界；不要为显而易见的赋值写注释。对外可见的行为变更应同步更新 `docs/设计/协议.md`、`docs/开发/测试指南.md` 或相关路线图。错误码、请求类型、fixture 字段和稳定性规则必须有文档来源，避免只存在于测试或实现中。

## 测试指南

测试以主机侧脚本为主，当前不依赖 pytest。新增测试文件建议命名为 `test_<feature>.py`，放在对应阶段目录下。测试必须可重复，不依赖真实时间、真实 Switch 服务或手写随机 fixture。涉及令牌、请求队列、nonce 消耗、PCTL 写入路径或错误码的改动，至少补充一个成功路径和一个失败路径。

## 真机验证与减少人员值守

真机探测、回归和候选验收必须先阅读并优先采用[真机稳定探测方法](docs/开发/真机稳定探测方法.md)和[无人值守更新与测试指南](docs/开发/无人值守更新与测试指南.md)。默认流程为“集中准备 → 一批实体操作 → 有界无人值守采集 → 离线机器核验与集中画面复核 → 恢复验收”，不要让操作者逐条盯日志、逐张确认截图或反复重启。

- 部署候选、重新开启采集或处理仅在启动时读取的配置前，先按无人值守指南检查免整机重启路径，优先沿用现有稳定进程或已验证的后台安全交接。必须区分重新打开 NRO、重新启动后台进程与整机重启；“配置只在后台启动时读取”不等于“必须重启主机”，单独重开 NRO 也不能冒充后台已重新加载配置。
- 现有自动更新入口对同一候选重载、当天已受限等情况有门禁。遇到这些限制，应先检查能否安全复用交接机制，并登记、修复和验证确有必要的工具缺口，不能直接把人工整机重启当作默认替代。不得绕过门禁、强杀后台，或伪造 release ID、制造无业务变更的新候选来触发热加载。只有验证目标本身需要真实重启，或已核实免整机重启路径不适用且没有可安全使用的替代方式时，才向用户提出正常重启，并说明本批具体原因与已排除的替代方式。
- 开始前固定验证口径、已证明事实、缺口、通过条件、候选/环境身份和执行顺序。代理先完成构建、前像与恢复材料核对、采集租期计算、日志/报告目录和操作清单准备，再一次性交付本批确实需要人的步骤；已有证据只在相同适用范围内引用，不重复无关实验。
- 优先沿用已有稳定进程与短时只读会话；当前 PCTL 能力探测使用标准后台同进程、编译期只读旁路，限定频率、租期、失败次数并释放所有会话。不得默认重走旧 BD23F1 自启动、HID/远程输入初始化、hbmenu/Controllers X+A、第二游戏切换或旧 BPC/PM 电源等已知故障路径；新服务或绕行方案先单独证明权限、生命周期与资源影响。
- 将本批确需的正常重启、POWER 真休眠/唤醒、实体底座拔插、必要的 PIN/长按与画面观察合并为最小操作清单，让用户完成一组后统一回复。采集租期只在后台启动读取，必须在实际激活或请用户重启前完成配置与回读，并绑定实际新进程、boot、候选身份及停止边界；不能以断网、停计时、关显示或宿主等待冒充真实休眠/设备计时。真休眠窗口只拉日志，不提交 status 或控制请求。
- 已授权且无需人的准备、采集、拉回、机器断言和报告工作继续执行。采集必须有结束条件、失败上限和可核验收尾，不因截图失败或独立画面复核待确认而阻塞可靠读数采集；设备 fatal、未知写入或恢复未证实则停止该设备上的依赖操作，先核对现场。
- 规则操作仅经已有标准请求队列提交一次，先记录 UUID/内容，再等待相同 ID/type 回执。超时保留未知，不能换 ID 重发或猜测回滚。原始 JSONL、请求/回执、source/run/boot、设备日期/tick、各 Result、带符号余额、完整 0x44 和 SHA-256 只追加保留；不回写旧 raw，跨 boot 不相减 tick。
- 截图默认只用 sys-botbase pixelPeek，不为截图初始化或注入输入。先整理带时间/阶段/哈希的 HTML 集中复核材料，再一次确认必须的真实观察；人工结论独立保存，不逐张进行不必要的 AI 读图，不以机器状态替代提示可见性、真实解除或游戏运行效果。
- 同进程诊断旁路及旧能力证据不自动资格化标准候选。正式验收仍绑定实际 clean 候选、生产组件与原始附件哈希；若现有标准采集无法提供必需证据，先登记工具缺口，在新候选中补齐采集支持、回归和身份验证，不拿带采集标记的 NSP 或旧报告顶替冻结生产二进制，也不降低资格门禁。
- 每个危险窗口保留既有超时与恢复事务，集中收尾不能延迟该窗口的恢复。按本次前像核验规则、完整 0x44/timer、启动项、原 NSP 和运行身份，只清除本批拥有的标记；确认停采不再追加，区分“SD 原像已恢复”和“下次正常启动才加载原二进制”。最终报告可离线重跑，分别记录通过、部分证明、未执行、前提不满足与恢复状态。

## 提交与 Pull Request 指南

历史提交主要使用简洁主题行，包含 `feat(scope): ...`、`docs(scope): ...`、`doc: ...` 等形式，也接受 `Initial commit` 这类基础提交。建议提交信息使用现在时，说明用户可观察的变化，例如 `feat(observe): validate offline code requests`。PR 应包含变更摘要、已运行的测试命令、风险点；涉及 UI、协议文件或真实设备流程时，附截图、样例 JSON 或设备验证记录。

在 Windows PowerShell 中创建包含中文的 commit message 时，必须使用无 BOM 的 UTF-8 message 文件再执行 `git commit -F <file>`；不要使用会写入 BOM 的 `Set-Content -Encoding utf8` 生成提交信息文件。可使用 `[System.Text.UTF8Encoding]::new($false)` 和 `[System.IO.File]::WriteAllText(...)` 写入。提交后必须运行 `git log -1 --pretty=%B` 验证标题前没有不可见 BOM；如果发现标题前有异常隐藏字符，立即用无 BOM message 文件 `git commit --amend -F <file>` 修正。

## 安全与配置提示

标准分发构建不提供运行时控制模式，并包含用于常驻启动的 `boot2.flag`；Device Lab 包默认不包含该 flag。`disable.flag` 阻止新的控制写入，但不阻止状态、诊断和恢复。无效 token、PCTL 写失败、result/ledger 失败和回滚路径不得消耗 nonce。不要在 child-visible UI、日志、fixture 之外的示例中泄露真实 `grant_secret`。

## Agent 专用说明

修改前先查看 `docs/开发/开发指南.md`、`docs/设计/协议.md` 和相关测试。优先保持变更小而可验证；不要重写无关文档或回退用户改动。新增行为时同时补测试和必要文档，并在最终回复中列出实际执行过的命令。

本仓库中文文档使用 UTF-8。PowerShell 默认编码或控制台显示可能把中文读成乱码；读取或写入 `AGENTS.md`、`docs/*.md` 等中文文档时，应显式使用 UTF-8，例如 `Get-Content <file> -Encoding utf8`，避免误判编码或把文档写坏。

涉及 C 编译或 package 验证时运行 `python tools/package_remote.py`；每次回归默认启用 `--with-eden` 打包并校验 Eden 模拟器测试应用（`build/eden-test/pctc-eden.nro`），不要绕过其中的构建产物清理、C/Python/UI 测试、Eden 测试应用和 Zip 校验。Host 缺少本地 `make` 或 C 编译器时，仍须在满足项目契约的 devkitPro 环境完成验证，并在最终汇报中明确列出 Eden 应用的打包与校验状态。
