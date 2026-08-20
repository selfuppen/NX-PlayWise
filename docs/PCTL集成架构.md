# PCTL 集成架构

PCTL 是 Nintendo Switch 的系统家长控制服务。PlayWise 标准分发构建只管理游玩额度、系统计时和提醒；它不把私有强制锁屏能力作为产品承诺。

## 隔离边界

```text
主机应用 / 游戏内浮窗
        │ Release request
        ▼
后台服务（sysmodule）安全状态机
        │ transaction + confirmation
        ▼
PCTL adapter
        │ 短时 pctl / pctl:s session
        ▼
Horizon PCTL
```

- NRO 与 Overlay 不直接访问 PCTL，也不决定 request 是否安全。
- `common/` 不包含 libnx、SD 路径或 0x44 raw layout。
- `platform/switch/pctl_adapter.c` 是私有命令和布局的唯一平台边界。
- host 测试使用 `PtcPctlStub` 注入写入、观察和恢复失败。
- Release 不持久化 capability matrix，也不提供 probe 请求。

## 启动预检与接管

首次启动在任何 setup 写入前执行只读检查：

1. release manifest 与编译版本一致；
2. `set:sys` 可读取 HOS、firmware digest 和产品型号；
3. 能确认是否运行于 Atmosphère；
4. PCTL service 可初始化并读取状态；
5. settings snapshot 长度、hash 和 layout 符合当前协议；
6. 没有无法恢复的启动遗留事务。

命中 OLED/HOS 22.5.0 基线记为 `verified`；其他结构正常组合记为 `accepted_unknown`，需要家长确认。snapshot、layout、启动遗留事务、PCTL 读取或 manifest 失败进入只读 `protection`，标准分发构建不允许绕过。

家长最终确认后才：

- 原子保存 `backups/install_pctl_snapshot.json`，且永不覆盖现有有效 snapshot；
- 解除当前限制并确认状态；
- 进入 5 秒同步宽限；
- 激活规则自动应用。

## 普通写入事务

设置今日总额度、加时、今日不限时、恢复周计划和 Enforce 共用恢复框架：

```mermaid
flowchart LR
    A["验证请求与安全门禁"] --> B["持久化 PCTL 与文件前像"]
    B --> C["应用目标并启动 timer"]
    C --> D["读取运行时状态"]
    D -->|"确认"| E["提交 state/result/nonce"]
    D -->|"等待传播"| F["applied_pending_confirmation"]
    F -->|"30 秒内确认"| E
    F -->|"超时"| G["精确回滚"]
    G -->|"无法证明恢复"| H["保护模式 + disable.flag"]
```

`disable.flag` 只阻止新的 PCTL 控制写入。PCTL status read、诊断和恢复继续工作，因此支持页在故障时仍可用。

## 私有命令证据

当前本机 libnx 源码不提供 `StartPlayTimer (1451)`、`GetPlayTimerRemainingTime (1454)`、`GetPlayTimerSpentTimeForTest (1952)`、`GetPlayTimerSettings (145601)` 或 `SetPlayTimerSettingsForDebug (195101)` 的公开封装。不得用“libnx 没有定义”推断参数单位或 0x44 raw 布局。

`1454` 返回剩余时长。虽然 `1952` 名称和返回形态看起来像已用时，但当前没有真机 A/B 证据证明它只在游戏前台运行时累计；已有设备现象显示它可能接近 Play Timer 启动后的墙钟时长。因此 Switch 适配层不读取 `1952`。限时日的“配置总分钟 − 1454 剩余分钟”也只能称为额度消耗估算，不能冒充实际游戏时间；不限时日返回“额度消耗估算暂不可用”。

2026-08-20 的真机诊断进一步证明：后台在午夜调用 `1451 StartPlayTimer` 后，即使用户没有打开屏幕或游戏，60 分钟额度仍会归零。标准分发的每日 Enforce 因而只同步并回读当天设置，绝不调用 1451；实际计时生命周期继续由 Horizon 管理。未来若要由 PlayWise 主动调用 1451/停止命令，必须先取得可靠的前台应用启动、挂起、恢复、退出证据，并完成 HOME、游戏前台、游戏挂起、主机待机和跨日场景的 A/B，不得仅凭“应用进程存在”推断游戏正在运行。

私有行为只能依据：

- 仓库协议和固定 layout adapter；
- host 端已知向量与失败注入；
- 当前候选构建产物在明确记录的机型/HOS 上进行 A/B 真机验证。

版本或 firmware digest 变化时，即使结构预检通过，也只能由家长接受为未知兼容；它不自动成为新的已验证基线。

## Device Lab

`raw_block`、`suspend`、危险 capability probe 和故障注入位于独立 Device Lab：

- Title ID `4200000000BD23F0`；
- IPC `pwtl:u`；
- SD 根目录 `sdmc:/switch/playwise-device-lab`；
- NRO 持续显示危险水印；
- package 默认无 `boot2.flag`；
- 不由标准分发目标 `make packages` 构建。

LAB 可绕过标准分发构建的兼容认证，但每个探针本身仍必须 snapshot、执行、确认并恢复；恢复无法证明时同样写入 LAB 根目录下的 `disable.flag`。任何 LAB 证据都不能直接作为标准分发构建的资格结果。
