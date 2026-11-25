> 长期参考：保留原调查日期、源码基线及未证明边界，不代表当前候选资格或可直接执行的步骤。见[调研分类](README.md)及[开发文档入口](../开发/README.md)。

[文档索引](../README.md) · 调研记录（不代表已发布能力或真机资格）

# 独立轻量 VI 受限原因弹窗可行性评估

调研日期：2026-11-25。本文针对“游玩受限弹窗提醒”的长期演进路线（方案 3），评估能否在不依赖第三方 `nx-ovlloader` / `Ultrahand` / `Tesla-Menu` 的前提下，由 PlayWise 独立提供轻量、只读的 VI 受限原因弹窗。本文仅记录基于固定本地源码与协议边界的可行性评估与实验方案，**不代表已实现或已取得真机资格**。

## 结论摘要

| 方案形态 | 是否依赖第三方管理器 | 内存与权限影响 | 故障隔离 | 评估结论 |
| --- | --- | --- | --- | --- |
| 现有阶段 A + B（外部 Overlay 协作） | 依赖 `Ultrahand >= 2.3.0` + `nx-ovlloader`（原版 Tesla 仅手动/隐藏后自动详情） | 复用外部管理器；主后台零新增服务 | 主后台与 UI 进程分离 | **当前落地基线**（适合已装 Ultrahand 用户，但受单槽与外部配置边界制约） |
| 方案 3A：直接内嵌进主后台 `pctc-sysmodule` | 否 | 突破 512 KiB 静态堆，须新增 `vi:m`/`nvdrv:s`/`pl:u`/`psc:m` | **差**：显示/驱动故障直接威胁核心 PCTL 强执行与事务恢复 | **明确否决** |
| 方案 3B：独立可选只读显示 sysmodule（零 HID 抢占） | **否**（同时兼容与 Tesla/Ultrahand 并存） | 独立进程估算约 `1.5–2.0 MiB` 物理堆 + `0x40000` `nvdrv` 传输内存 | **高**：崩溃或关闭不影响主后台 PCTL 控制，可单独启停 | **技术上可行，列为推荐实验方向**（须先通过 4 项真机边界实验） |

核心建议：如果未来需要彻底摆脱对 Ultrahand 版本、单槽 `nx-ovlloader` 切换以及 `boot_package.ini` 预加载权衡的依赖，**不要修改主控制后台的权限与内存边界**，而应构建一个**独立、可选、只读的轻量 VI 显示 sysmodule**——仅负责展示受限原因与提醒时剩余时间快照，**绝不抢占手柄焦点**，加时码兑换与家长授权操作继续交给既有 `playwise.ovl`（或原生官方密码）。

## 1. 为什么不能内嵌进主控制后台 `pctc-sysmodule`

核对仓库当前 `sysmodule/sysmodule.json` 与 `sysmodule/source/main.c`：

1. **极小静态堆与系统内存池约束**：主后台运行在 `pool_partition: 2`（`PoolPartition_System`），静态内堆仅为 `PTC_INNER_HEAP_SIZE = 0x80000`（512 KiB，`sysmodule/source/main.c:41`）。仅 `nvdrv` 默认传输内存（8 MiB）或压缩后的 `0x40000`（256 KiB）加双缓冲帧缓冲（448 KiB）就已超过主后台全部静态堆。
2. **最小权限面原则**：`sysmodule/sysmodule.json` 当前仅请求 `fsp-srv`、`time:u`、`set:sys`、`spl:`、`pctl`、`pctl:s`、`omm` 七个服务，不接触 `vi:m`、`nvdrv:s`、`pl:u`、`psc:m` 或 `hid:sys`。
3. **控制与呈现必须故障隔离**：主后台承担 PCTL 写入事务、规则落盘、防重放 nonce 与安装前快照恢复。一旦将 GPU/VI/共享字体逻辑塞入同一进程，休眠切换竞态、图层句柄异常或内存耗尽都会直接导致控制后台异常退出或触发 `fatal`，破坏核心家长控制可靠性。

## 2. 独立可选只读显示 sysmodule 架构设计（方案 3B）

### 2.1 职责与单向只读数据流

- **独立进程与可选部署**：分配独立 Title ID 与独立的 `boot2.flag`，不作为标准核心控制的硬前置依赖；删除或停用该显示模块时，PlayWise 的计时、限制、恢复与手动浮窗完全不受影响。
- **只读订阅 `pctc:u`**：复用现有 `pctc:u` 可选唤醒事件（命令 4）与只读原因快照，不新增 PCTL 会话，不写 `rules.json`、`state.json`、`used_nonces.jsonl` 或任何第三方 INI。
- **零 HID 输入抢占**：不申请 `hid:sys`，不调用 `requestForeground`（区别于需要交互焦点的 `playwise.ovl`）。因此弹窗不会阻断游戏手柄操作，也不会拦截 Nintendo 原生家长控制弹窗上的 PIN 输入。
- **自动退让与限时退出**：
  - 读取现有 `input_owner` 租约文件：当 `playwise.ovl` 自身详情已处于可见前台时，只读弹窗主动清屏退让，避免与完整浮窗叠层。
  - 达到展示时长（如 10 秒）或收到原因解除/交接/停用信号时自动清屏释放活跃绘制，仅在状态变化时提交帧，空闲期零刷屏。

### 2.2 VI 显示层、帧缓冲与内存预算

依据本地 `../libnx`（`vi.h`、`framebuffer.c`、`nv.c`、`pl.c`）与仓库 vendored `libtesla`（`companion/overlay/vendor/libtesla/include/tesla.hpp`）核对：

1. **VI 图层建立**：
   - 调用 `viInitialize(ViServiceType_Manager)` 打开 `vi:m` 服务，经 `viOpenDefaultDisplay` 打开 `"Default"` 显示设备。
   - 调用 `viCreateManagedLayer(&display, (ViLayerFlags)0, 0, &layer_id)` 与 `viCreateLayer` 绑定图层（参考 `tesla.hpp:1112-1122`），通过 `viSetLayerSize` / `viSetLayerPosition` 定位在屏幕左上角安全区域，并加入 `ViLayerStack_Default`（是否加入 `ViLayerStack_Screenshot` 须在真机实验中评估隐私与取证需求）。
   - Z 序通过 `viGetZOrderCountMax` 查询上限后设置；若需与 `nx-ovlloader`（通常使用最大 Z 序）和平共存，应实测次高 Z 序能否同时满足“压过 Nintendo 原生家长控制遮罩”与“低于显式呼出的 Overlay”。
2. **`nvdrv:s` 与传输内存压缩**：
   - `framebufferCreate` 内部依次执行 `nvInitialize()`、`nvMapInit()`、`nvFenceInit()`（`framebuffer.c:36-46`）。在 `AppletType_None` 环境下，`nv.c:31-62` 自动选择 `NvServiceType_System` 并连接 `"nvdrv:s"`。
   - libnx 默认弱符号 `__nx_nv_transfermem_size = 0x800000`（8 MiB，`nv.c:11`）。参考 `tesla.hpp:3825` 覆盖为 `u32 __nx_nv_transfermem_size = 0x40000;`（256 KiB），可将 `nvdrv` 传输内存压低 32 倍。
3. **紧凑帧缓冲精确计算**：
   - 采用与当前简要提示一致的 `448 × 256` 像素画布及 `PIXEL_FORMAT_RGBA_4444`（2 BPP）。
   - 按 `framebuffer.c:58-82` 的块线性（`NvLayout_BlockLinear`，`block_height_log2 = 4`，即 16 GOBs = 128 行对齐、64 字节行宽对齐）公式计算：
     - `width_aligned_bytes = (448 * 2 + 63) & ~63 = 896` 字节；
     - `height_aligned = (256 + 127) & ~127 = 256` 行；
     - 单缓冲大小 `fb_size = 896 * 256 = 229,376` 字节（`224 KiB`）；
     - 双缓冲（`num_fbs = 2`）页对齐总大小 `buf_size = 458,752` 字节（`448 KiB`）。
   - 相比 Tesla 默认 `448 × 720` 双缓冲（对齐高度 `768`，总计 `1,376,256` 字节 ≈ `1.31 MiB`），紧凑卡片直接节省约 **67%** 帧缓冲内存。
4. **共享字体与有界字形缓存**：
   - 经 `plInitialize(PlServiceType_User)` 连接 `pl:u`（`pl.c:25,32` 明确避开 HOS `16.0.0+` 上 `pl:s` 返回 `LibnxError_IncompatSysVer` 的限制），映射 `SHAREDMEMFONT_SIZE = 0x1100000`（17 MiB 只读共享内存）。该区域仅消耗虚拟地址空间，不消耗本进程物理堆。
   - 复用[Overlay 内存与死机风险自查](Overlay内存与死机风险自查.md)的有界字形缓存策略，仅缓存当前三语简要文案所需的少量字形（上限 `64–128 KiB`），并沿用 `companion/notification_text.c` 现有三语格式化器与 `tests/notification_layout/` 容量模型。
5. **总体内存估算**：

| 内存项 | 估算大小 | 说明 |
| --- | --- | --- |
| `nvdrv:s` transfer memory | `256 KiB`（`0x40000`） | 覆盖 libnx 弱符号 `__nx_nv_transfermem_size` |
| `448×256` `RGBA_4444` 双缓冲 | `448 KiB` | `framebufferCreate` 块线性对齐分配 |
| 有界字形缓存 + FreeType 工作区 | `128 – 256 KiB` | 仅渲染 26px 标题 + 最多 4 行短文案，严格上限 |
| 栈、IPC 缓冲与代码运行时开销 | `~512 KiB` | 单线程事件循环（或主线程 + 唤醒事件） |
| **预计物理堆需求** | **约 `1.5 – 2.0 MiB`** | 须在目标 HOS 与多 sysmodule 共存环境下实测系统内存池余量 |

### 2.3 电源休眠、底座切换与共存护栏

- **真实休眠安全（`psc:m`）**：依据 `../libnx/nx/include/switch/services/psc.h`，注册 `pscmGetPmModule` 监听电源状态变更；收到 `PscPmState_ReadySleep`、`PscPmState_ReadySleepCritical` 或 `PscPmState_ReadyShutdown` 时，立即清屏、停止一切 `framebufferBegin`/`End` 提交并在最短时间内调用 `pscPmModuleAcknowledge`，防止阻塞主机进入休眠或在唤醒后触发显示驱动挂死。
- **掌机 / 底座切换（`omm`）**：复用 `omm` 通知（或按需重新查询 `viGetDisplayResolution` / `viGetDisplayLogicalResolution`），在 720p 与 1080p 切换时重新计算安全边距与图层坐标，避免电视过扫描裁切文字。
- **与 `nx-ovlloader` / 第三方 Overlay 并存**：
  - 不使用 `nx-ovlloader` 槽位，因此无论用户安装了原版 Tesla-Menu、Ultrahand 任意版本、还是完全未安装浮窗管理器，受限原因提示均可独立出现。
  - 若用户同时安装了 `nx-ovlloader`（开启 `playwise.ovl` 或 Ultrahand），两者各自持有独立的 managed layer 与 `nvdrv:s` 会话，必须实测并发创建与切换的稳定性。

## 3. 事实、推论与未证明边界

| 分类 | 具体条目 |
| --- | --- |
| **源码已证实事实** | 1. `pctc-sysmodule` 静态堆为 `0x80000`（512 KiB），运行于 `PoolPartition_System` 且未申请任何显示/图形/字体/电源管理服务。<br>2. libnx `framebufferCreate` 在 `AppletType_None` 下连接 `nvdrv:s`，默认 `__nx_nv_transfermem_size` 为 `0x800000`；vendored `libtesla` 在 `tesla.hpp:3825` 将其降至 `0x40000`。<br>3. `448×256` `RGBA_4444` 经 16-GOB 对齐后双缓冲精确占用 `458,752` 字节（448 KiB）。<br>4. HOS `16.0.0+` 上共享字体须经 `pl:u` 映射只读 `0x1100000` 共享内存（`pl.c:32`）。<br>5. `psc:m` 提供标准休眠前通知与 ACK 接口（`psc.h:89-102`）。 |
| **工程推论** | 1. 独立只读显示 sysmodule 的物理堆可压在 `1.5–2.0 MiB` 量级，且崩溃或停用不影响主后台控制逻辑。<br>2. 不申请 `hid:sys` 即可彻底避免焦点抢占与游戏输入冲突。<br>3. 复用 `pctc:u` 唤醒事件与 `input_owner` 租约，可在不增加轮询压力的情况下与 `playwise.ovl` 协作退让。 |
| **未证明的真机边界** | 1. **原生家长控制遮罩 Z 序**：在 HOS `22.5.0`（及 `21.0.0–23.0.1`）下，独立 sysmodule 的 `vi:m` 图层在原生“今天的时间已用完”全屏弹窗上是否 100% 可见、不被遮罩覆盖。<br>2. **多 managed layer 并发**：与 `nx-ovlloader` / `Ultrahand` 同时常驻时，双 `vi:m` 图层与双 `nvdrv:s` 会话是否受限于系统层数上限或存在 Z 序抖动。<br>3. **系统内存池天花板**：在同时运行 `nx-ovlloader` 及大型 3D 游戏时，`PoolPartition_System` 是否稳定容忍新增 `~1.5–2.0 MiB` 常驻进程，连续休眠/唤醒与底座热插拔后是否存在内存/句柄泄漏或 `fatal`。<br>4. **性能与输入零干扰**：无 `hid:sys` 图层在高频动画游戏中开启/清屏时，是否对游戏帧率或原生 PIN 输入产生任何可观测卡顿。 |

## 4. 建议的最小真机实验门禁（立项前置）

在决定投入正式产品开发前，应先以独立实验 NSP（不改动生产 `4200000000BD2300`）按以下四步顺序完成真机验证，任一步出现 `fatal`、休眠失败或系统内存池不足即停止扩张：

1. **实验 E1（独立 VI 静态卡片 + 零输入影响）**：在未安装或停用 `nx-ovlloader` 时，独立实验 sysmodule 通过 `vi:m` + `nvdrv:s (0x40000)` + `pl:u` 显示/清除 `448×256` 静态卡片，验证大型游戏运行流畅、手柄输入无抢占、无内存/句柄增长。
2. **实验 E2（原生家长控制遮罩可见性）**：触发真实每日/护眼/就寝原生限制弹窗，使用 `pixelPeek` 采证确认卡片在原生全屏遮罩之上清晰可读，且原生官方 PIN 输入正常。
3. **实验 E3（与 Ultrahand / `nx-ovlloader` 共存）**：同时启用 `nx-ovlloader` + Ultrahand + 实验 sysmodule，验证开机无崩溃、打开/隐藏 `playwise.ovl` 时 `input_owner` 退让无重影、Z 序稳定。
4. **实验 E4（真实休眠与底座热插拔）**：连续执行至少 5 次 `POWER` 真休眠/唤醒与掌机↔电视底座热插拔，核验 `psc:m` 及时 ACK、无黑屏/花屏/延迟休眠，并检查系统内存池余量。

## 5. 证据来源与固定版本

| 来源 | 固定版本 / 状态 | 核对日期 |
| --- | --- | --- |
| 当前仓库 `sysmodule/sysmodule.json`、`sysmodule/source/main.c` | 工作区提交 `7b160bc5` 基线 | 2026-11-25，本地 |
| 仓库 vendored `WerWolv/libtesla` | `f766e9b607a05e9756843cbd62b3bfb98be1646c` + `companion/overlay/vendor/libtesla/UPSTREAM.txt` | 2026-11-25，本地 |
| 本地 `../libnx` | `feebd026ca0f5dcc2119f46ad8e0d16ad3dd4973`（`v4.12.0-25-gfeebd026`，工作树干净） | 2026-11-25，本地 |
| 本地 `../Atmosphere` | `5388824be146a89619e8d641acd64599cf1c5f62`（tag `1.11.2`，工作树干净） | 2026-11-25，本地 |
| 关联调研记录 | [非 PCTL 暂停与浮窗能力调研](非PCTL暂停与浮窗能力调研.md)、[Overlay 内存与死机风险自查](Overlay内存与死机风险自查.md)、[系统与大气层最低版本依赖调研](系统与大气层最低版本依赖调研.md) | 2026-11-25，本地 |
