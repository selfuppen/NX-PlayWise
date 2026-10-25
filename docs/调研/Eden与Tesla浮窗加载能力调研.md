> 长期参考：保留原调查日期、源码基线及未证明边界，不代表当前候选资格或可直接执行的步骤。见[调研分类](README.md)及[开发文档入口](../开发/README.md)。

[文档索引](../README.md) · 调研记录（不代表已发布能力或真机资格）

# Eden 与 Tesla 浮窗加载能力调研

调研日期：2026-10-24。本文记录 Eden 模拟器能否加载 PlayWise 的 Tesla 浮窗（`playwise.ovl`），以及能否通过 Tesla、Ultrahand 一类组件间接加载。结论只描述模拟器能力缺口，不改变标准分发协议、控制策略或真机资格判定。

## 结论

Eden 当前**不能**运行 PlayWise 浮窗，也没有可安装的 Tesla／Ultrahand 组件能绕过这一限制。

1. **没有 CFW 层**。Tesla 与 Ultrahand 在真机上都依赖 Atmosphère 的 sysmodule 自启与进程创建扩展；Eden 是 HLE 模拟器，`sdmc:/atmosphere/contents` 只被当作 LayeredFS mod 根目录，不存在 sysmodule 自启，`ldr:pm`、`ldr:dmnt`、`ldr:shel`、`pm:shell` 的命令全部是空实现。Ultrahand 本身是 sysmodule，因此比 Tesla 更不可行。
2. **当 NRO 直接启动会在 `Renderer::init()` 里 fatal `0xA72`**。Eden 按 `NRO0` magic 识别内容，`playwise.ovl` 改名后能进入 `tsl::loop`，随后在 VI 阶段以 `ResultPermissionDenied (0xA72)` 终止。
3. **第一个根因是 managed layer 的 owner 不匹配**。libtesla 以 `aruid = 0` 调用 `viCreateManagedLayer`，再通过 `_viOpenLayer`（`.in_send_pid = true`）打开同一层。Eden 的 `OpenLayer` 不读报文里的 aruid，一律替换为调用者进程号（本批为 81），与 owner 0 不相等即返回 `VI::ResultPermissionDenied`。探测 10 证明：owner 改成调用者自己的 aruid 后 OpenLayer 立即成功，所以这是**语义差异**，不是该路径不可用。
4. **第二个根因是层几何命令未实现**。即便 owner 对齐，`ISystemDisplayService 2203 SetLayerSize`、`2201 SetLayerPosition`、`1202 GetZOrderCountMax` 在 Eden 中仍是 `nullptr`；libtesla 对前两者用 `ASSERT_FATAL`，失败即 `fatalThrow`，所以浮窗仍然无法显示。
5. **第三个缺口是 Home／Sleep 按键事件**。`hid:sys 101 AcquireHomeButtonEventHandle`、`121 AcquireSleepButtonEventHandle` 未实现，libtesla 后台轮询拿不到事件句柄，HOME／电源隐藏路径不成立。
6. **Eden 只完整支持 applet 的默认窗口路径**。生产 NRO 在 Eden 里能显示，是因为它作为 applet 运行：libnx `__nx_win_init()` → `viCreateLayer` 检测到 aruid≠0 → `appletCreateManagedDisplayLayer`（AM 内部用**进程自己的 PID** 作 owner 建层）→ `viSetLayerScalingMode` → `nwindowCreateFromLayer`，全部命中 Eden 已实现的命令。libtesla 不走这条路径。
7. 设置项 `LibraryApplet.enable_overlay` 与 `.ovl` 加载无关：它在 `am/applet_manager.cpp` 里创建的是任天堂自己的 `AppletProgramId::OverlayDisplay` 小程序，不是 overlay 加载器。

因此 Eden 的 L4 分层结论保持不变：只验证 NRO 业务与 UI，不承担 Tesla／CFW 生命周期（见[分层测试策略](../开发/分层测试策略.md)）。

## 调研对象与基线

| 项目 | 值 |
| --- | --- |
| 模拟器 | Eden Development Build `master-a43664c0fd-master`（本机 `F:\Eden-MCP-Chinese\eden.exe`，eden-control protocol 2） |
| MCP | `eden-mcp 0.5.0`，基础 commit `a43664c0fd9bb56d2bb4ef3b4f1943a19b767066` |
| 上游源码核对 | `https://git.eden-emu.dev/eden-emu/eden`，master 分支，查询日期 2026-10-24 |
| 浮窗实现 | 仓库内 vendored libtesla，上游 `https://github.com/WerWolv/libtesla` commit `f766e9b607a05e9756843cbd62b3bfb98be1646c` |
| 被测产物 | 探测时使用 `build/switch/playwise.ovl`，SHA-256 `6304567a2553010971c2086d84ce6da7f2a419af3d0a7723547296529569c140`；当前工作区同名产物为 `b2ce097742b93f45234632140429aa2ed97472d15cf4c27798e7624c89287167`，两者不是同一份二进制 |
| 固件／键 | profile 内 HOS 22.5.0；`AppletType_None` 进程的 `appletGetAppletResourceUserId()` 为 0，applet 进程为 81 |

本地 libnx 与上游 libnx 都不包含这些私有 VI 命令的封装语义，本文只用 Eden 上游源码与实际 A/B 读数，不据缺失封装推断参数单位。

## 真机上的浮窗需要哪些部件

| 部件 | 真机机制 | Eden 现状 |
| --- | --- | --- |
| 加载器 sysmodule（ovlloader 或 Ultrahand） | `/atmosphere/contents/<tid>/exefs.nsp` + `boot2.flag`，由 CFW 在启动时载入 | 无 sysmodule 自启。全仓检索 `boot2`、`exefs.nsp`、`ovlloader`、`ovlmenu`、`NextLoadPath` 均无命中；`atmosphere/contents` 仅作为 LayeredFS mod root 打开 |
| 进程创建／启动 | `ldr:pm CreateProcess`、`ldr:dmnt SetProgramArgument`、`pm:shell LaunchProgram` | `ldr.cpp` 三张表、`pm.cpp` 的 `pm:shell` 0–9 全为 `nullptr`；`sm.cpp` 仅 `65100 AtmosphereHasService` 有实现 |
| 浮窗显示 | managed VI layer（owner = 调用者 aruid）、z-order、`viSetLayerSize/Position` | managed layer 可建可开，但 owner 语义与 libtesla 的 0 不兼容；`2201`／`2203`／`1202` 未实现 |
| 浮窗输入与唤起 | hidsys Home/Sleep 按键、HID 轮询、`pmdmntGetApplicationProcessId` | 按键事件未实现；HID 轮询、焦点切换、应用 PID 正常 |
| 浮窗切换 | `envSetNextLoad` / `NextLoadPath` | NRO 加载器只写 `MainThreadHandle`、`AppletType`、`Argv`、`EndOfList` 四项，不提供 `NextLoadPath` |

Eden 的系统服务在 `services.cpp` 里是内置的 HLE 进程列表，没有“从 SD 卡启动 guest sysmodule”的路径；KIP 加载器（`AppLoader_KIP`）只支持把 KIP 当内容文件启动，不是开机 sysmodule 流程。

## 探测结果

探测脚本为一次性工具（`build/eden-probes/20261025-overlay-load/probe.py`），使用隔离 profile 复制 `config/keys/nand`，通过 eden-control 启动内容并抓日志；探测 NRO 源码在同目录 `vi-probe*/source/main.c`。

| 批次 | 内容 | 结果 |
| --- | --- | --- |
| `run-680f1cbcc575` | 改名为 `playwise-overlay.nro` 的 `playwise.ovl` | 进入 `tsl::loop<PctcOverlay>` → `tsl::gfx::Renderer::init()` 内 fatal `0xa72 (2114-0005)`，无浮窗 |
| `run-4e961fcde7fb` | `AppletType_None` 下逐条复刻 `Renderer::init` | `viCreateManagedLayer(aruid 0)` OK；`viCreateLayer`（managed，`in_send_pid`）返回 `0xA72` |
| `run-da1b9ae381b4` | stray／AM／真实 aruid 三种取层方式对比 | manager `2012 CreateStrayLayer` 返回 `0x5F59`；`appletCreateManagedDisplayLayer` 失败（`0x1159`）；`OpenLayer` 一律 `0xA72` |
| `run-ffde25235462` | **A/B**：同一显示层，仅切换 `in_send_pid` | 不附 PID → `viCreateManagedLayer(aruid 0)` + `OpenLayer` 均 OK，native window size 60；附 PID（libnx 原始行为）→ `0xA72` |
| `run-12e348fa6136` | `AppletType_None` + `viInitialize(Default)`（普通 homebrew 默认路径） | `viCreateLayer(stray)` 失败：日志显示 `2012(CreateStrayLayer)` 未实现 |
| `run-6db452c1ea22` | applet 进程，libnx 默认窗口 + libtesla 的 managed 层 | 默认窗口 `1280x720`、framebuffer 正常；libtesla 路径 `viCreateLayer(managed)` 仍 `0xA72`；`1202` 未实现 |
| `run-e15b45c4aad2`、`run-014902fe55b1` | applet 进程，managed layer 的 owner 改为**调用者自己的 aruid** | 两份输出逐字节一致：`viCreateLayer`、`viSetLayerScalingMode`、`nwindowCreateFromLayer`、`framebufferCreate` 全部 OK；仅 `2203`／`2201`／`1202` 失败 |
| `run-fb264d226651` | 上一行流程 + 8 次 `viAddToLayerStack` + 绘制一帧 + libtesla 的后台依赖 | 层栈、framebuffer 写入与提交、`hid`、`pl` 字体、`pmdmntGetApplicationProcessId`（返回 81）、`hidsysEnableAppletToGetInput` 全部 OK；`101/121` 按键事件未实现 |

`0x5F59`、`0x1159`、`0x1759` 都是 libnx 侧结果码（module `0x159`），分别为 `InvalidCmifOutHeader`、`NotInitialized`、`BadInput`：Eden 对未实现命令只记 Critical assert 而不构造回复，调用方因此拿到“无效 CMIF 回复”。`0xA72` 是 Eden 的 VI 结果（module `0x72`，description 5，`ResultPermissionDenied`）。`0xE401` 是内核 `InvalidHandle`，出现在 `svcGetProcessId`、以及探测里对**未初始化服务**的调用上（探测 9 漏了 `plInitialize`，探测 11 补上后正常返回），不能当作 Eden 缺服务。

关键日志片段：

```
Service.Fatal <Error> ThrowFatalError: Threw fatal error type 0 with error code 0xa72
Debug <Critical> assert Unknown / unimplemented function '2012(CreateStrayLayer)': port='IManagerDisplayService'
Debug <Critical> assert Unknown / unimplemented function '2203(SetLayerSize)': port='ISystemDisplayService'
Debug <Critical> assert Unknown / unimplemented function '2201(SetLayerPosition)': port='ISystemDisplayService'
Debug <Critical> assert Unknown / unimplemented function '1202(GetZOrderCountMax)': port='ISystemDisplayService'
Debug <Critical> assert Unknown / unimplemented function '101(AcquireHomeButtonEventHandle)': port='hid:sys'
Debug <Critical> assert Unknown / unimplemented function '121(AcquireSleepButtonEventHandle)': port='hid:sys'
```

## 根因对照（Eden 上游源码 + A/B）

1. **OpenLayer 的 owner 校验与调用者 PID**。libnx `_viOpenLayer()` 以 `.in_send_pid = true` 发送 `IApplicationDisplayService` 命令 `2020`。Eden `cmif_serialization.h` 对 `ClientProcessId`（`ClientAppletResourceUserId` 的别名）走 `ArgumentType::InProcessId` 分支，直接写 `args.pid = ctx.GetPID()`，报文原始值被丢弃；而 `CreateManagedLayer` 的参数类型是 `AppletResourceUserId`（`ProcessId`），按 raw memcpy 读取，libtesla 传入的 0 被原样保存为 owner。`Container::OpenLayerLocked()` 中 `R_UNLESS(layer->GetOwnerAruid() == aruid, VI::ResultPermissionDenied)` 因此必然失败。libtesla 之所以在真机上能过，是因为它在 `AppletType_None` 进程中运行，`appletGetAppletResourceUserId()` 也返回 0，两边同为 0；在 Eden 里调用者 PID 恒为真实进程号，0 永远对不上。
2. **层几何命令缺失**。`ISystemDisplayService` 表中 `1202 GetZOrderCountMax`、`2201 SetLayerPosition`、`2203 SetLayerSize` 均注册为 `nullptr`，只有 `2204/2205/2207` 等少数命令有实现。libtesla `Renderer::init()` 的顺序是 `viSetLayerScalingMode` →（`viGetZOrderCountMax`，允许失败）→ 8×`viAddToLayerStack` → `viSetLayerSize`（`ASSERT_FATAL`）→ `viSetLayerPosition`（`ASSERT_FATAL`），所以即使 owner 对齐，浮窗也停在 `viSetLayerSize`。
3. **未实现命令的后果**。Eden `service.cpp::ReportUnimplementedFunction()` 记录 Critical assert 后不构造回复，调用方拿到 libnx `InvalidCmifOutHeader`；本批探测中该断言之后进程仍被 `Break` 终止，因此不能把“命令返回失败”当作可继续运行的信号。
4. **Home／Sleep 按键事件缺失**。`hid:sys` 的 `101`／`121` 是 `nullptr`；libtesla 的后台轮询用 `hidsysAcquireHomeButtonEventHandle`／`AcquireSleepButtonEventHandle` 关闭事件来实现 HOME／电源键隐藏，这两条路径在 Eden 中拿不到句柄（libtesla 不检查返回值，因此不会立即崩溃，但隐藏行为不存在）。
5. **无 NextLoadPath**。NRO 加载器只在设置 `program_args` 时写入 4 项 `LoaderConfig`（`MainThreadHandle`、`AppletType`、`Argv`、`EndOfList`），没有 `NextLoadPath` 项，`envSetNextLoad` 的同一进程换内容流程不成立。

## Eden 实际支持什么（生产 NRO 为何能显示）

生产 NRO 用 libnx 的弱默认 `__nx_applet_type = AppletType_Default`，`__nx_win_init()` 内 `viCreateLayer` 发现 `appletGetAppletResourceUserId()` 非 0，转而调用 `appletCreateManagedDisplayLayer`；Eden 由 AM 的 `display_layer_manager` 以 `AppletResourceUserId{m_process->GetProcessId()}` 作 owner 建层，随后 OpenLayer 的调用者 PID 恰等于该 owner，校验通过。层几何由 AM 预先设置，libnx 只再调用 `viSetLayerScalingMode` 和 `nwindowCreateFromLayer`，两条都已实现，所以窗口正常。

`AppletType_None` 的 NRO 则走 stray layer（`2030`，或 manager `2012`），而 `2012` 未实现，因此这类内容在 Eden 里连窗口都拿不到——这也是本轮多条探针在 `AppletType_None` 下无输出的原因之一。

## 要让 Eden 支持需要什么（不在 PlayWise 范围）

- sysmodule 自启或内置 overlay host；
- `ldr:pm`／`pm:shell`／`ldr:dmnt` 的进程创建与参数设置；
- VI `2201`／`2203`／`2012`／`1202` 的实现；
- `ClientProcessId` 语义与真实 HOS 对齐（要么保留报文值，要么让 owner 与调用者一致）；
- `hid:sys 101/121` 的 Home／Sleep 按键事件。

这些都是模拟器实现工作，PlayWise 侧修改无法补齐；即使补齐，浮窗生命周期与稳定性资格仍只能由真机证据判定。

## 未证明与不再重走的边界

- 只测试了上面这一个 Eden 构建与 MCP 版本；其他分支或更新版本是否补齐 VI/HID 未验证。
- 未尝试修改 libtesla 的 owner 参数或 `AppletType` 来“骗过”Eden。探测 10／11 已证明 owner 对齐能让层与 framebuffer 工作，但那条路径仍会撞上 `2203`，而且改动 vendored 浮窗加载语义会偏离生产行为，不能作为浮窗可用性的证据。
- 未测试通过 KIP 内容文件启动 sysmodule；即便可行也不是 Atmosphère 的开机自启语义。
- `0x5F59` 的具体来源（断言中止还是缺失回复）只按日志顺序描述，未逐命令区分。
- `pmdmntGetProcessId` 对 qlaunch（`0x0100000000001000`）的查询在本批返回 `0x20F`，只记录未深究；libtesla 的 `requestForeground` 忽略该返回值，因此不影响上面的结论，但对 applet 的焦点收回在 Eden 中实际未生效。
- 探测包含多次“启动后未真正进入 guest”的批次（Eden 记录 `Loading Unknown program`，或探针自身在 `AppletType_None` 下因 stray layer 失败而提前退出），这些批次已排除在结论之外；同类探针需重跑直到拿到 sdmc 输出。

## 证据位置

原始产物在本地 `build/eden-probes/20261025-overlay-load/`（`build/` 已被忽略，不作为仓库证据）：

| 文件 | SHA-256 |
| --- | --- |
| `run-ffde25235462/sdmc/vi-probe6.txt` | `ba9e639489464ff13d6d85b472667d34628f5c9d95f0851c7d4e2afecacb9461` |
| `run-6db452c1ea22/sdmc/vi-probe9.txt` | `d030efccf962f5a304f9ef532149bc4ebe3b531186a7ca02aa7b09a7d380b924` |
| `run-e15b45c4aad2/sdmc/vi-probe10.txt` | `62f6888dda0ee3f47469c823281da796595920d0af0e44d91d5f8a1d49534c4d` |
| `run-014902fe55b1/sdmc/vi-probe10.txt` | `62f6888dda0ee3f47469c823281da796595920d0af0e44d91d5f8a1d49534c4d`（与上一份逐字节一致） |
| `run-fb264d226651/sdmc/vi-probe11.txt` | `3aff483071b35ed060f3a4ad14fe9533a93383043e79860aa694f90ce7804570` |
| `run-fb264d226651/eden-log.txt` | `c8920dd1cbe807bf7199d0165f22b9610c4ce46b09dc989e3ee520e2afda9777` |
| `run-12e348fa6136/sdmc/vi-probe8.txt` | `bbeff6750dee3d22c047d4850b65946a8d81d6a4fef5c9823a721651a9bc2e72` |
| `run-680f1cbcc575/eden-log.txt` | `3e506da40d879f3c0759ab91be13e142cd67572f94e9884905d4bbf4d8b6512c` |

`run-680f1cbcc575/identity.json` 记录了被测 ovl 的源路径、SHA-256 与 `scope: Eden overlay runtime probe; not a product gate`。上述批次是一次性能力探测，不作为 Eden 回归用例，也不进入发布门禁。
