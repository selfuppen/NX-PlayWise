# 开发指南

本项目优先实现稳定、可测试的核心闭环。第一个有价值的里程碑不是精致 UI，而是在 host 环境证明 token、request queue、control policy、backup gate、nonce 消费和失败路径，然后再接触真实 PCTL 写入。

## 语言与运行时

- `common`、`sysmodule`、`companion` 默认使用 C。
- `common` 不得依赖 libnx、文件系统、SD 卡路径、UI、真实时钟或进程级可变平台状态。
- Python 用于开发工具，例如 fixture 生成、协议 probe 和 package 生成。
- 不引入 C++，除非后续决策明确记录原因。

如果后续引入 C++：

- 使用 `-fno-exceptions`。
- 使用 `-fno-rtti`。
- 避免复杂静态初始化。
- 避免在 sysmodule 热路径使用不可控动态分配。

## 依赖方向

允许方向：

```text
companion      -> common
sysmodule      -> common
sysmodule      -> platform adapters
tests          -> common
tests          -> test doubles
tools          -> protocol-compatible fixture logic
```

禁止方向：

```text
common         -> libnx
common         -> SDMC paths
common         -> UI
common         -> process-global mutable platform state
business logic -> raw PCTL u16 layout
```

## 模块职责

`common`：

- Token 编码、解码和 HMAC 校验。
- day index、weekday 和 bedtime 时间计算。
- 规则评估和控制策略。
- request/result schema 常量。
- 稳定错误码、reason 和中文 message 映射。
- nonce 消费时机决策。

`platform`：

- `StorageVTable`、`PctlVTable`、time provider、logger。
- host doubles：`mem_storage`、`pctl_stub`、fake time。
- 后续真实 SDMC/libnx/PCTL adapter 必须停留在本层。

`sysmodule`：

- boot lifecycle 和服务初始化。
- request queue 扫描与 stuck processing 恢复。
- config/rules/state/capabilities 加载。
- backup 编排、event logging、nonce ledger。
- 调用 PCTL adapter 并写入 result。

`companion`：

- 孩子状态页、离线码输入、request file 创建。
- matching result 等待和展示。
- PIN 保护的家长区与本地规则编辑。

`tools`：

- 授权码生成。
- deterministic fixture 生成。
- 协议 probe。
- SDMC package 生成。

`tests`：

- C host 单元测试。
- `mem_storage + pctl_stub + fake time` 集成测试。
- Python 协议和 fixture 回归测试。

## 稳定性规则

- 默认配置是 `control_mode: "observe"`。
- 默认 package 不包含 `boot2.flag`。
- `disable.flag` 覆盖所有 control mode。
- 未知 `control_mode` 降级为 `observe`。
- 坏 JSON、未知 schema 和未知 request type 不得触碰 PCTL。
- 能解析到 request id 的请求应尽量写出结构化 result。
- 无效 token 不得消费 nonce。
- dry-run 不得消费 nonce。
- PCTL 写失败不得消费 nonce。
- backup 失败必须阻止写入。

## 合并前检查

- 改动逻辑是否能在无 Switch 的 host 测试中运行？
- `observe` 是否避免写入和 nonce 消费？
- 每个写路径是否先要求 backup？
- 高风险路径是否检查 capability gate？
- 错误码是否有稳定 reason 和中文 message？
- child-visible UI 或日志中是否避免泄露真实 `grant_secret`？
