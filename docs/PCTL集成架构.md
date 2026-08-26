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
- 再次确认当前 `0x44`、timer、今日总额度和剩余时长仍与只读采集结果一致；
- 把当前今日策略保存为仅当天有效的接管规则，并直接激活自动控制；
- 不把今日临时改成不限时，也不调用私有命令 `1451`。若验证期间状态发生变化，则保留现状并拒绝接管。

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

2026-08-25 的 Device Lab run `1787669306-5331fd74a2626e81` 沉淀为历史证据：原始报告的六个 phase 项均非空，`restriction_effect.after.1455.value=true`，并且最终 `restoration.proved=true`、`restore_verdict=exact_restore_proved`。原始附件仍保持 `manual_observation:null`；操作者事后补充确认，真机在第六阶段关闭浮窗后出现了用户可见的时间限制以及暂停或退出提示。该人工补充与机器报告分别保存，不回写历史 JSON。由此可确认当前 OLED/HOS 22.5.0 候选环境中“当日 0 分钟 + 启动 timer”能够触发用户可见限制，并确认该轮恢复精确成功，但这仍是 Lab-only 证据，不提升为标准分发的禁玩、暂停或退出能力。

同一 run 的 `home_started` 在 `1454` 已保持 0 时仍记录到 `1952` 增长 75 秒，继续支持“HOME 下主动调用 1451 不安全”的结论，也再次说明 `1952` 不能直接解释为游戏前台游玩时长。旧报告只在限制阶段末尾读取一次 `1457`，结果为 `known=true, signaled=false`，不能排除中途短暂触发；`1458` raw 调用成功而 libnx 返回 `0x4B59`，旧报告仍写出 `value_equal:true`，该相等值不可用于语义判断。后续仍需验证 `1457` 连续锁存、初始化真实 HOS 版本后的 `1458` raw/libnx 对照、`0x44` 当日范围外变化，以及 HOME/前台/挂起/待机的完整生命周期矩阵。

2026-08-26 的增强 run `1787681083-b411b3488b91fee0` 使用 `2.0.2-alpha+d1fa4dcdfa11` 完成六个自动阶段并得到 `exact_restore_proved`。限制阶段 `1455=true`，timer 从 true 变为 false，`1952` 和负值 `1454` 均只变化约 1 秒；操作者另行确认真机出现了用户可见的时间限制、暂停或退出提示。原始附件保持 `manual_observation:null`，因此仍是 `complete:false`，且该见证只能证明提示可见，不能证明游戏实际暂停或退出。

该 run 在 15 秒内检查 1457 共 131 次且始终未触发，说明 1457 不能可靠代表用户已经看到限制提示；1458 的 raw/libnx 对照已变为 `comparable:true, value_equal:true`。HOME 主动计时仍由 `1952` 精确增长 75 秒再次证明不安全。原始全零 0x44 写入后变化为头部字节 `0、1、2` 和当日字节 `39、41`：前者与当前适配器显式写入的 `0x0101/0x0001` 完全一致，属于实现预期头初始化而非意外变化，但不据此推断更深层 HOS 语义。由于基线全零，游戏前台、挂起和待机三个静态阶段不足以形成生命周期结论；后续完整模式应先配置非空 Nintendo 周计划。

私有行为只能依据：

- 仓库协议和固定 layout adapter；
- host 端已知向量与失败注入；
- 当前候选构建产物在明确记录的机型/HOS 上进行 A/B 真机验证。

版本或 firmware digest 变化时，即使结构预检通过，也只能由家长接受为未知兼容；它不自动成为新的已验证基线。

## Device Lab

`raw_block`、`suspend`、危险 capability probe、引导式生命周期取证和故障注入位于独立 Device Lab：

- Title ID `4200000000BD23F0`；
- IPC `pwtl:u`；
- SD 根目录 `sdmc:/switch/playwise-device-lab`；
- NRO 以中文状态向导提供“启用实验后台 / 恢复正常后台 / 查看本轮完整报告”三个事务化入口，按当前 run 精确绑定报告，不把旧报告或草稿当成本轮完成结果；专用 Overlay 提供默认聚焦模式和高级六阶段模式并持续显示危险水印；
- package 默认无 `boot2.flag`；
- 不由标准分发目标 `make packages` 构建。

NRO 在启用时记录标准与 Lab `boot2.flag` 的原状态：若标准后台原本启用，将其原子改名为唯一旁路备份，再创建 Lab flag；恢复时先请求 Lab sysmodule 恢复并证明会话前 PCTL 状态，证明成功或尚未创建会话后，才删除由该事务创建的空 Lab flag，并按 journal 精确恢复标准 flag 原本存在或不存在的状态。已有备份、未知 Lab flag、journal 损坏或外部并发修改都不会被覆盖，而是进入恢复提示。因此标准与 Lab sysmodule 不会同时竞争 PCTL；完整流程开始和结束各重启一次。该事务不修改标准二进制、配置、规则、凭据、日志或业务备份。

NRO 的状态检查只读取现有 flag、journal 与 session；真正切换仍由原事务函数完成。恢复等待必须异步刷新页面，只有 `exact_restore_proved` 或从未创建过会话时才允许恢复正常 boot flag。中文错误页先说明是否发生过更改和下一步，再按需展开结果码、事务阶段、请求 ID 与路径。

专用 Overlay 通过 `pwtl:u` 和 Lab SD 根目录提交固定状态机请求。默认 `restriction_quick` 只执行限制效果，适合按游戏重复验证；高级 `full` 保留原六阶段资格取证。普通阶段异步采样 75 秒；真实限制阶段只在用户对“无未保存进度的非关键游戏”长按确认后执行，sysmodule 写入 0 分钟并启动 timer，同时设定独立的 15 秒恢复期限。Overlay、进程或主机中断后，持久化阶段仍可继续；限制期限已过时后台优先恢复。恢复必须同时证明完整 `0x44` 和 timer 与会话前一致，否则写入 Lab `disable.flag` 并拒绝后续写入，只保留立即恢复能力。

限制阶段自动恢复成功后，会话保持 `awaiting_observation`：恢复请求不能跳过或清除待提交的人工观察，Overlay 在此状态隐藏立即恢复入口，NRO 只引导操作者返回浮窗。人工观察分别记录提示可见性和游戏实际继续、暂停/挂起、退出或无法确定；不得把提示文案等同于实际退出。聚焦模式要求 1/1、完整模式要求 6/6，且两层观察与 `exact_restore_proved` 均齐全时才发布正式报告。

Overlay 的普通阶段、观察选项、重试和立即恢复同时支持手柄与触摸；限制阶段的两秒确认不接受触摸。Device Lab 构建固定加载 Switch 简体中文共享字体，不依赖当前系统语言。损坏的 `session.json` 不得按“尚未开始”处理，界面必须停止新阶段并引导保留现场。

报告把命令可调用性、wire shape 与产品语义分开。`1006/1031/1035/1457/1458` 保存 raw/libnx 对照和 `comparable`；任一侧失败时相等判定固定为 false。sysmodule 在采集真实 HOS 版本后初始化 libnx 版本状态，再执行带版本门禁的公开 wrapper。`1451–1455`、`145601/195101`、Lab-only `1952` 保存原始值和 Result；限制阶段每 100 ms 非阻塞检查并锁存 `1457`，保存检查次数和首次触发单调时间，同时保存阶段前后完整 `0x44` hex、目标星期、预期字节范围、全部变化 offset 与范围外变化 offset。报告还嵌入实际 `environment.json` 和 Device Lab `build.json`，从而把机型、HOS、固件摘要与候选构建绑定。`1952` 不进入 Release adapter 的状态读取路径。任何 Lab 证据都不能直接作为标准分发构建的资格结果。
