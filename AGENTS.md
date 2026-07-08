# Repository Guidelines

## 项目结构与模块组织

本仓库是 Nintendo Switch 游玩时间控制项目的本地验证版本，优先保证协议、队列和控制策略可在主机侧测试。

- `common/`：跨端协议常量与未来 C 核心代码，例如 `common/protocol/*.h`、`common/token/*.h`。
- `tools/`：Python 开发工具与参考实现，包括令牌生成、协议探测、请求队列和 observe 处理器。
- `tests/`：主机侧测试脚本与 fixtures。`tests/mvp/` 覆盖离线加时令牌，`tests/observe/` 覆盖 observe 请求流。
- `docs/`：架构、协议、测试与真实设备验证文档。开始开发前先读 `docs/README.md`。

## 构建、测试与开发命令

当前没有统一构建系统；主要通过 Python 脚本验证行为。

- `python tools/make_fixtures.py`：重新生成确定性的令牌 fixture。
- `python tests/mvp/test_token_v1.py`：运行 MVP 令牌编码、解码和 CLI 一致性测试。
- `python tests/observe/test_observe_queue.py`：运行 observe 请求队列和 dry-run 行为测试。
- `python tools/grant_code.py --minutes 30 --device test-device --secret test-secret --day-index 2380 --nonce 4660`：生成离线加时代码示例。
- `python tools/protocol_probe.py init --root <tmp-dir> --device <id> --secret <secret>`：初始化本地协议目录用于手工探测。

## 编码风格与命名约定

Python 代码使用 4 空格缩进、类型注解和 `from __future__ import annotations`。保持工具脚本无第三方依赖，便于在早期主机测试中直接运行。文件、函数和变量使用 `snake_case`；协议常量使用全大写，例如 `TOKEN_VERSION`。C 头文件保持小型、稳定、无平台依赖，`common` 不应引用 libnx、SD 卡路径、UI、真实时钟或进程级可变状态。

## 注释与文档要求

新增复杂协议逻辑时，在代码附近添加简短注释说明位布局、错误原因或安全边界；不要为显而易见的赋值写注释。对外可见的行为变更应同步更新 `docs/PROTOCOL.md`、`docs/TESTING.md` 或相关路线图。错误码、请求类型、fixture 字段和稳定性规则必须有文档来源，避免只存在于测试或实现中。

## 测试指南

测试以主机侧脚本为主，当前不依赖 pytest。新增测试文件建议命名为 `test_<feature>.py`，放在对应阶段目录下。测试必须可重复，不依赖真实时间、真实 Switch 服务或手写随机 fixture。涉及令牌、请求队列、nonce 消耗、PCTL 写入路径或错误码的改动，至少补充一个成功路径和一个失败路径。

## 提交与 Pull Request 指南

历史提交主要使用简洁主题行，包含 `feat(scope): ...`、`docs(scope): ...`、`doc: ...` 等形式，也接受 `Initial commit` 这类基础提交。建议提交信息使用现在时，说明用户可观察的变化，例如 `feat(observe): validate offline code requests`。PR 应包含变更摘要、已运行的测试命令、风险点；涉及 UI、协议文件或真实设备流程时，附截图、样例 JSON 或设备验证记录。

## 安全与配置提示

默认控制模式必须保持 `observe`，默认包不应包含 `boot2.flag`。`disable.flag` 优先于所有控制模式。无效 token、dry-run、PCTL 写失败和备份失败路径不得消耗 nonce。不要在 child-visible UI、日志、fixture 之外的示例中泄露真实 `grant_secret`。

## Agent 专用说明

修改前先查看 `docs/DEVELOPMENT_GUIDE.md`、`docs/PROTOCOL.md` 和相关测试。优先保持变更小而可验证；不要重写无关文档或回退用户改动。新增行为时同时补测试和必要文档，并在最终回复中列出实际执行过的命令。
