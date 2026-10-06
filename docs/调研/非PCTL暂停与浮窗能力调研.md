[文档索引](../README.md) · 调研记录（不代表已发布能力或真机资格）

# 非 PCTL 暂停与浮窗能力调研

调研日期：2026-10-06。本文只记录源码能力与待验证设计，不变更标准分发协议、控制策略或资格状态，也不代表真机验证已经通过。

## 结论

在 PlayWise 已要求的 Atmosphère 环境中，可以通过非 PCTL 路径显示自定义浮窗；也存在取得游戏进程句柄、请求暂停和恢复线程的完整接口路径。两项能力需要分别实现：显示层和输入焦点不会自动停止游戏逻辑。

| 目标 | 可行路径 | 当前证据与限制 |
| --- | --- | --- |
| 手动打开游戏内页面 | 现有 Tesla Overlay：VI 显示层 + HID 输入控制 | 已有实现，不依赖 PCTL；没有暂停游戏的调用 |
| 到点自动弹出页面 | 常驻的 UI 接收后台事件，或与浮窗管理器/加载器集成 | 技术上可实现；当前 PlayWise 没有此自动唤起链路 |
| 暂停并恢复游戏进程 | `pm:dmnt` 的 Atmosphère 扩展 + `svcSetProcessActivity` | 固定版本源码给出完整路径；需要新增权限和真机实验 |
| 借助 Cheat 服务暂停 | `dmnt:cht` 的暂停/恢复命令 | 上游已有接口；涉及调试器附着、作弊加载和其他工具的共享状态 |
| 弹出 Nintendo 原生页面 | AM / Library Applet，例如 Error、Web | libnx 有封装，但当前后台与 Tesla 浮窗没有相应 AM 身份和代理会话；不是直接可用的后台弹窗入口 |
| 实现持续的全局限玩 | 暂停、计时、前台识别、UI、恢复和新游戏监测组成独立控制器 | 上述单项能力不足以替代现有 PCTL 产品行为 |

## 1. 暂停游戏：优先调查进程活动接口

### 已确认的接口链路

1. `pmdmntGetApplicationProcessId` 获取 application PID。
2. `pm:dmnt` 的 Atmosphère 扩展命令 `65000 AtmosphereGetProcessInfo` 返回目标进程的 **copy handle**、`ProgramLocation` 与 `OverrideStatus`。
3. 对该普通进程句柄调用 `svcSetProcessActivity(handle, ProcessActivity_Paused)`；恢复使用 `ProcessActivity_Runnable`。SVC 编号为 `0x4F`。

本地 libnx 提供步骤 1 和步骤 3 的公开封装，没有 `pmdmntAtmosphereGetProcessInfo` 封装；步骤 2 需要按固定版本接口编写平台适配，不能把 PID 或 debug handle 当作普通进程句柄。

Atmosphère 1.11.2 的 PM 实现从进程列表取得普通 handle，再通过 IPC copy handle 返回。Mesosphère 的 `SetProcessActivity` 校验枚举和目标句柄、拒绝操作调用者自身，然后调用 `KProcess::SetActivity`。后者请求挂起目标的全部线程；恢复清除这些线程的 `SuspendType_Process` 请求。新建线程也会继承父进程的这一挂起状态。

直接进程活动路径没有附着调试器或加载 cheat 的步骤，适合作为首选实验候选。源码只能证明接口链路与该内核实现，不能证明所有游戏在目标 HOS 上都能可靠暂停、恢复。

### 接入与恢复边界

- 当前 [sysmodule 权限配置](../../sysmodule/sysmodule.json) 不包含 `pm:dmnt` 或 SVC `0x4F`，因此不能直接在现有后台中调用。
- application PID 不等于“当前正在前台运行的游戏”。HOME 上也可能保留 application，游戏接管 hbmenu 时也可能占用该槽位；需要核对程序身份、override 状态，并建立可靠的前台/后台判定。
- 进程暂停状态不是按调用者计数的锁。源码在重复暂停、恢复未暂停进程时返回 `InvalidState`；不能把这些错误统一当成成功，也不能在暂停失败后无条件恢复。HOME、睡眠和其他进程活动控制者之间的同类状态冲突必须实验验证。
- `Runnable` 只清除 process suspend 原因。线程还可能处于 debug 或其他挂起状态，因此“恢复调用成功”不能独立证明游戏已经恢复游玩。
- 该实现没有把暂停状态绑定到请求者 handle 的存活期。不能假定关闭 handle、关闭浮窗或后台崩溃会自动恢复；实验应设置独立恢复路径和短期限。
- 每次动作都要核对 PID、程序身份和已持有的 handle。游戏退出或切换后不能把旧暂停实例应用于新游戏。
- 冻结 CPU 线程不等于游戏自身的暂停菜单。系统时钟、网络服务器和系统服务不因此一起停下；联网超时、恢复后的时间跳变、音频/GPU 队列与存档期间的行为仍需检查。

### 替代路径：Atmosphère Cheat 服务

固定版本 `dmnt:cht` 定义了：

| 命令 | 名称 |
| --- | --- |
| 65003 | `ForceOpenCheatProcess` |
| 65004 | `PauseCheatProcess` |
| 65005 | `ResumeCheatProcess` |
| 65006 | `ForceCloseCheatProcess` |

暂停实现使用 `BreakDebugProcess`，恢复通过调试事件管理器继续进程。`ForceOpenCheatProcess` 的附着流程还会读取 cheats/toggles 文件并取得 debug handle；它也可以附着到 HBL。已有 Cheat 进程时，强制打开直接复用当前状态。

这条路径可用于独立实验，但不能假定它是无副作用的通用暂停器。服务可用性、调试附着冲突、已有作弊配置及其他工具正在暂停的状态，都需要单独处理。

## 2. 显示浮窗：现有技术不需要 PCTL

仓库固定的 [libtesla](../../companion/overlay/vendor/libtesla/UPSTREAM.txt) 已提供必要机制：

- `Renderer::init` 使用 `ViServiceType_Manager` 创建 managed layer，将其置于较高 Z 序，并创建 framebuffer；可以在该图层中绘制自定义文字、按钮、倒计时和页面。
- `requestForeground` 使用 `hid:sys` 命令 503 调整 application 与系统 applet 的输入许可。
- 该名称在这里表示输入处理；实现没有调用进程暂停或 AM 的前台切换。不能从浮窗已打开推断游戏已停止。
- Tesla `.ovl` 在此实现中使用 `AppletType_None`，与 Horizon 的 `AppletType_OverlayApplet` 是不同的运行方式。

现有 [PlayWise 浮窗入口](../../companion/overlay/source/main.cpp) 通过 `tsl::loop<PctcOverlay>` 运行，状态查询与页面更新属于已加载的浮窗。默认 framebuffer 为 448×720，适合侧边页面；更大的覆盖页面可以通过显示层实现，但需要另行调整布局与资源预算。

### 自动弹出需要增加常驻接收者

现有 libtesla 主循环在隐藏状态等待 `comboEvent`，此时不运行普通 GUI 更新；`show()` 自身只处理显示动画和回调，后台不能跨进程调用这个 C++ 对象。`setNextOverlay()` 使用当前 loader 的 `NextLoadPath` 在切换时加载下一份 `.ovl`，也不是后台远程唤起接口。

调研的 nx-ovlloader v1.0.7 源码在同一进程内加载一份 `.ovl`，退出后按 `NextLoadPath` 切换，默认回到 `ovlmenu.ovl`；没有提供本文所需的外部“弹出 PlayWise 页面”IPC 命令。该结论只绑定到所查版本，不代表其他管理器或 fork。

可选实现方式：

1. **与实际安装的浮窗管理器/加载器集成**：由常驻接收者读取后台事件，在合适时机加载 PlayWise 并唤醒完整显示与输入流程。需要核对实际管理器版本；只修改当前 `playwise.ovl` 不足以覆盖“当前加载的是其他浮窗”的情况。
2. **独立常驻提示 UI**：使用 VI 层显示轻量提示或休息页面，并与控制后台通信。可继续复用现有浮窗完成加时、PIN 等操作；需验证多浮窗之间的图层顺序、输入交还、HOME/睡眠和资源占用。

浮窗渲染与暂停控制应分开。UI 只显示后台状态并发出请求，恢复期限由后台与独立恢复机制负责，不能依赖页面一直打开。

## 3. 原生系统弹窗与“页面”的边界

libnx 的 Error Applet 用 `appletCreateLibraryApplet(..., LibAppletMode_AllForeground)` 启动系统页面；Web Applet 也有封装。它们需要调用者拥有适当的 AM 代理和 applet 上下文。

当前 PlayWise 后台与 Tesla 浮窗都使用 `AppletType_None`。把变量改成 `SystemApplet` 或 `OverlayApplet` 不能单独证明 AM 会接纳该进程；需要系统侧的身份、权限和生命周期支持。`appletRequestToGetForeground` 也有明确的 applet 类型限制；`appletSetFocusHandlingMode` 控制自身失焦行为，不能直接用来暂停另一个游戏。

Web Applet 还受应用 URL 白名单、内容挂载等约束，不能根据 API 名称推断它可以从后台显示任意 SD 卡 HTML。若“页面”指 PlayWise 自己的交互页面，VI 自定义 UI 更直接。

`ovln:snd` 在 libnx 中提供原始通知消息队列，但头文件没有给出本文所需的自定义文本/按钮页面协议。消息入队不等于系统已显示任意页面。`notif:*` 的公开封装是闹钟通知，也不足以证明任意即时弹窗能力。本文不把这两项列为已确认的通用页面路径。

## 4. 对 PlayWise 的建议与最小真机实验

先在隔离的实验组件中验证 **普通进程句柄 + SVC 0x4F + 自定义 VI 页面**。通过后再决定是否用于护眼休息或独立限玩后端；不应直接替换现有 PCTL 执行器。

实验顺序：

1. 官方家长控制关闭时，只显示/关闭提示页面，检查游戏继续运行、手柄输入交还、HOME 与睡眠。
2. 对一个明确识别的离线测试游戏请求短时间暂停并恢复。预先建立独立恢复入口；记录 PID、程序身份、原始 Result 和动作时间，并人工确认游戏逻辑及输入实际停止/恢复。
3. 检查重复请求、游戏已退出、HOME 已挂起、睡眠/唤醒、浮窗关闭和控制后台退出；证明失败不会错误恢复其他控制者的暂停状态，也不会把暂停实例转移给新游戏。
4. 另测 dmnt/调试工具并存、音频、存档边界与恢复后的时间行为。联网游戏需要单独的资格范围。
5. 最后验证自动弹出在 PlayWise 浮窗未加载、其他浮窗正打开和 HOME 上的行为。

绕开 PCTL 后，现有额度读数与护眼累计也不能原样沿用：当前协议依赖 PCTL 配置分钟数减剩余分钟数。完全独立模式还需要自己的使用时间口径、前台识别、睡眠排除、持久化和换日策略。暂停某个进程本身不会阻止用户关闭它后启动另一款游戏，也不会提供 Nintendo 的原生密码解限和全局限玩流程。

Host/Eden 可以验证控制状态机、失败处理与页面布局，不能证明真实进程挂起、AM 权限或 VI/HID 并存行为。若后续加入 C/C++、权限配置或打包变更，仍须执行 `python tools/package_remote.py`，默认 Eden 应用继续构建与校验；真机验证另行记录。

## 5. 证据来源与版本

本次先读取仓库文档、vendored libtesla 和本地 libnx。执行 `git -C ../Atmosphere rev-parse HEAD`、`describe --tags --always --dirty`、`status --short` 时该目录不存在，因此后续使用 GitHub 固定 tag 对应的已跟踪源码，没有增加或升级构建依赖。

| 来源 | 绑定版本 | 读取/查询日期 |
| --- | --- | --- |
| 仓库协议与测试指南 | 本次工作区；调研开始时 `git status --short` 为空 | 2026-10-06，本地 |
| 本地 switchbrew/libnx | `dbcc1beafc6b47b5ffbeb8ba82463a7d45da40bb`，`v4.12.0-12-gdbcc1bea`，工作树干净 | 2026-10-06，本地 |
| vendored WerWolv/libtesla | `f766e9b607a05e9756843cbd62b3bfb98be1646c` + `UPSTREAM.txt` 列出的本地修改 | 2026-10-06，本地 |
| Atmosphere-NX/Atmosphere | tag `1.11.2` → `5388824be146a89619e8d641acd64599cf1c5f62` | 2026-10-06，GitHub |
| WerWolv/nx-ovlloader | tag `v1.0.7` → `13295c6d15db23767223f2045c5b3689ea52723b` | 2026-10-06，GitHub |

仓库历史文档引用的 Atmosphère 调查 commit `8e96a489033b8597ec76c0055373642848d93af0` 在本次 GitHub 查询中返回 422、无法取得。因此本文使用上表的 release tag commit，既不改写历史记录，也不声称两个 commit 行为等价。

关键源码定位：

- [libnx：SVC 0x4F 的特权说明](https://github.com/switchbrew/libnx/blob/dbcc1beafc6b47b5ffbeb8ba82463a7d45da40bb/nx/include/switch/kernel/svc.h#L1218)。
- [Atmosphère：pm:dmnt 65000 接口签名](https://github.com/Atmosphere-NX/Atmosphere/blob/5388824be146a89619e8d641acd64599cf1c5f62/libraries/libstratosphere/include/stratosphere/pm/impl/pm_debug_monitor_interface.hpp#L31)、[服务返回 copy handle](https://github.com/Atmosphere-NX/Atmosphere/blob/5388824be146a89619e8d641acd64599cf1c5f62/stratosphere/pm/source/pm_debug_monitor_service.cpp#L71)、[PM 取得目标普通 handle](https://github.com/Atmosphere-NX/Atmosphere/blob/5388824be146a89619e8d641acd64599cf1c5f62/stratosphere/pm/source/impl/pm_process_manager.cpp#L432)。
- [Mesosphère：SetProcessActivity 校验与分派](https://github.com/Atmosphere-NX/Atmosphere/blob/5388824be146a89619e8d641acd64599cf1c5f62/libraries/libmesosphere/source/svc/kern_svc_activity.cpp#L62)、[KProcess 的线程挂起/恢复与状态检查](https://github.com/Atmosphere-NX/Atmosphere/blob/5388824be146a89619e8d641acd64599cf1c5f62/libraries/libmesosphere/source/kern_k_process.cpp#L1022)、[线程挂起原因](https://github.com/Atmosphere-NX/Atmosphere/blob/5388824be146a89619e8d641acd64599cf1c5f62/libraries/libmesosphere/include/mesosphere/kern_k_thread.hpp#L53)。
- [Atmosphère：Cheat 暂停命令](https://github.com/Atmosphere-NX/Atmosphere/blob/5388824be146a89619e8d641acd64599cf1c5f62/stratosphere/dmnt/source/cheat/dmnt_cheat_service.hpp#L24)、[调试暂停/恢复实现](https://github.com/Atmosphere-NX/Atmosphere/blob/5388824be146a89619e8d641acd64599cf1c5f62/stratosphere/dmnt/source/cheat/impl/dmnt_cheat_api.cpp#L340)、[附着与加载 cheats 的流程](https://github.com/Atmosphere-NX/Atmosphere/blob/5388824be146a89619e8d641acd64599cf1c5f62/stratosphere/dmnt/source/cheat/impl/dmnt_cheat_api.cpp#L783)。
- [nx-ovlloader：同进程 NRO 加载与 NextLoadPath](https://github.com/WerWolv/nx-ovlloader/blob/13295c6d15db23767223f2045c5b3689ea52723b/source/main.c#L151)。
- 仓库实际使用的 [libtesla 实现](../../companion/overlay/vendor/libtesla/include/tesla.hpp)：`requestForeground` 约 282 行、`Renderer::init` 约 1039 行、`setNextOverlay` 约 3514 行、隐藏等待与显示循环约 3535 行。这里以 vendored 文件为准，不能用上游原文件覆盖本地兼容修改。
- [libnx：AM 类型与代理初始化](https://github.com/switchbrew/libnx/blob/dbcc1beafc6b47b5ffbeb8ba82463a7d45da40bb/nx/source/services/applet.c#L139)、[Error Applet 前台调用](https://github.com/switchbrew/libnx/blob/dbcc1beafc6b47b5ffbeb8ba82463a7d45da40bb/nx/source/applets/error.c#L10)、[Web Applet 约束](https://github.com/switchbrew/libnx/blob/dbcc1beafc6b47b5ffbeb8ba82463a7d45da40bb/nx/include/switch/applets/web.h#L305)。

本次实际执行了 `git status --short`、上述上游版本检查、`rg` 源码搜索、UTF-8 文档读取，以及固定 commit 的 GitHub API/raw 源码查询。没有改动运行代码，没有执行设备控制、编译或回归；Eden 应用本次未打包、未校验。
