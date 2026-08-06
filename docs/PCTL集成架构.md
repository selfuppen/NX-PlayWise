# PCTL 与 Playwise 集成架构关系

本文档专门梳理并总结 Nintendo Switch 原生家长控制服务（PCTL）与 Playwise 项目之间的架构关系、交互原则及容错机制。

## 1. 什么是 PCTL？

PCTL（Parental Control Service/Session）是 Nintendo Switch 系统自带的官方家长控制模块，负责在系统底层管理和执行游玩时间限制、年龄分级、就寝时间（Bedtime）和系统级拦截。

**Playwise 的核心目标**是通过第三方令牌和策略引擎，对官方 PCTL 的时间限额和状态进行动态控制与覆写，从而实现灵活的加时、临时解锁与自定义规则，而无需依赖任天堂官方手机 APP。

## 2. 整体架构与隔离原则

为了保证 Playwise 业务逻辑的纯粹性与可测试性，本项目在架构上对 PCTL 进行了严格的隔离设计：

```text
家长端 / Companion NRO / Tesla Overlay (客户端)
        │ 提交通用 Request (IPC / 队列)
        ▼
Sysmodule (调度中心)
        │ 验证 Token，解析规则
        ▼
Platform Adapters (环境适配层 - PCTL Adapter)
        │ 隔离平台调用
        ▼
Switch OS PCTL (真实系统家长控制服务) / Host PCTL Stub (测试桩)
```

### 核心隔离规则

1. **客户端不可见 PCTL**：Companion NRO 与 Tesla Overlay 作为纯前端展示与输入层，**绝不允许**直接调用任何 PCTL 接口。它们只能向 sysmodule 提交如 `set_today_limit` 的标准请求。
2. **纯 C Core 的业务隔离**：`common/` 下的控制策略、Token 验证等业务代码完全不知道 PCTL `0x44` raw layout 的存在，也不依赖 `libnx`。
3. **Platform Adapter 适配**：所有与 PCTL 的真实交互（如获取状态、写入配置）均被封闭在 `platform/` 层的 PCTL Adapter 中。在 Windows 开发环境中，我们注入 `PtcPctlStub`（Mock）来替代真实的 Switch PCTL，实现脱机验证。

## 3. PCTL 在 Playwise 中的具体交互职责

Playwise 的 Sysmodule 通过 PCTL Adapter 实现对系统的接管，主要交互包括：

*   **状态查询 (Status Read)**：
    *   定期轮询或按需读取当前是否处于 `restricted`（受限）状态。
    *   通过 `GetPlayTimerRemainingTime (1454)` 读取剩余游玩时间（注意其返回值为有符号的 `s64` 纳秒级数据，过期时间可能为负数，由 Adapter 钳制并转为分钟）。
*   **设置覆写 (Settings Write/Apply)**：
    *   根据当天的规则（或 Token 解锁），重写 PCTL 的限制策略（如 `flag=0x0600, enabled=0x0100` 等）。
    *   通过 `StartPlayTimer (1451)` 确保新限制在运行时立即生效。
*   **能力探测 (Capability Probes)**：
    *   验证底层是否具备强禁玩 (`probe_raw_block`) 或暂停进程 (`probe_suspend`) 的能力，在通过实机 A/B 对比探针后才开放高阶限制动作。

## 4. 安全、容错与恢复机制

直接修改原生 PCTL 设置存在导致设备永久锁定的风险，因此 Playwise 引入了严苛的事务保障：

*   **安装快照保护 (Install Snapshot)**：
    系统在首次接管 PCTL 时，会在 `backups/install_pctl_snapshot.json` 保存原始完整的 PCTL 配置。任何灾难性错误都可以退回该安全基线。
*   **前像事务 (Before Image Transaction)**：
    每一次普通的 PCTL 写入前，Sysmodule 必须在 `recovery/active/` 下持久化本次修改前的前像。
*   **防呆与 Fail-Open (禁用标志)**：
    如果写入失败、运行时目标不符，或恢复失败，系统将立刻清理能力记录，创建 `disable.flag` 以完全禁用 Playwise 控制权，并确保让 Switch 回归原始（或不限时）状态，防止死锁（Fail-Open 原则）。
*   **资源合规**：
    PCTL Adapter 强制只使用短时会话 (`pctl` / `pctl:s`) 进行通信，避免长期持有单会话 (`pctl:a`) 导致与系统组件冲突。

## 5. 应对上游环境 (libnx) 缺失的对策

由于当前的社区 `libnx` 并未公开部分 PCTL 私有封装：
*   缺少 `StartPlayTimer (1451)`、`GetPlayTimerSettings (145601)`、`SetPlayTimerSettingsForDebug (195101)` 宏。
*   文档规定**绝不能**强行用 libnx 中无关的数据结构推断这些私有 API 参数。
*   因此，涉及 PCTL Play Timer Settings Layout v2（如 0x44 长度的二进制块）的相关协议开发，我们只能并且必须依靠**真实设备的 A/B 测试证据**与本项目 `docs/协议.md` 中的规范约定来进行读写映射，保证与系统固件的二进制兼容。

---
**总结**：PCTL 是 Playwise 发挥作用的最终舞台。在 Playwise 的设计中，PCTL 既是执行最终物理拦截的系统底座，也是必须要被极其谨慎、高度隔离和具有完善事务回滚机制来管理的外部环境依赖。
