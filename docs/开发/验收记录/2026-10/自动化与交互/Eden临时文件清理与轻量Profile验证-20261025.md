# Eden临时文件清理与轻量Profile验证（2026-10-25）

结论：不需要为每批测试完整复制Eden。当前定制版没有独立配置目录的启动参数，但可通过同卷程序硬链接、相邻user目录联接和独立profile复用程序及固件资源。本批原型的A首次设置、B全新启动、A重开持久化均通过，三个原生进程健康退出；来源安装未变化。正式`eden_runner.py run`尚未切换此模式，本批不宣称main17项在新模式全部通过。

## 测试计划已保存

原后续计划位于提交`8260ff6f`的[D盘迁移与独立HID采样回归](D盘迁移与独立HID采样回归-20261024.md#后续覆盖与日常测试)：X02网页配对→发码→NRO兑换、X01地区日历导入与应用、X04指定日期就寝与跨页草稿。清理前另存Git原文和哈希，保存副本SHA-256为`8c2c58c851608893612c71aa12ea2ab045acec82963df4f608a511ff50f32d00`，不依赖待删除运行目录。

保存证明与副本在[plan-saved.json](../../../../../device-reports/eden-cleanup-audit/20261025/approved-deletion/plan-saved.json)及[saved-next-test-plan.md](../../../../../device-reports/eden-cleanup-audit/20261025/approved-deletion/saved-next-test-plan.md)。日常仍执行Host确定性回归和必要统一构建；Eden业务集中在发版前及明确授权的受影响验证。

## 已批准的历史临时文件清理

用户批准此前列出的284个精确组件后，重新核对了71份运行目录的所有权、来源一致性和进程状态，仅删除重复的exe、languages、keys、NAND系统Contents。保留所有原始报告、截图、日志、SD业务状态、配置、可变NAND存档、来源安装、MCP依赖和既有私有归档；身份不明的一个overlay探测目录未删除。

| 范围 | 删除组件数 | 删除逻辑大小（GB，十进制） |
|---|---:|---:|
| D盘test-runs | 40 | 3.968 |
| F盘test-runs | 104 | 10.317 |
| H盘eden-runtimes | 52 | 5.158 |
| H盘两份旧probe及20份overlay probe | 88 | 8.730 |
| 合计 | 284 | 28.173 |

删除前完整哈希复核，执行使用PowerShell LiteralPath及固定根目录门禁，每项删除追加日志；删除后284目标均不存在，3,521个保留文件和51份原始报告核验通过，来源组件不变。卷剩余空间观测增量分别为D 3,971,784,704、F 10,327,465,984、H 13,902,647,296字节；它包含文件系统分配等影响，不冒充逐文件物理释放的精确计量。

机器结果：[cleanup-result.json](../../../../../device-reports/eden-cleanup-audit/20261025/approved-deletion/cleanup-result.json)。原批准清单保持原状，执行授权、删除流水和核验结果单独追加。

## 为什么不能直接给现有exe传一份配置

先检查本机`../eden-mcp`的clean源码：commit `fe8c44e499013be1772790c990656b2ef1585a4d`，tag `v0.5.0`。SDK启动GUI仅使用exe及`-g`等参数，effective profile优先选择exe相邻user；它设置的`EDEN_USER_DIR`不证明原生进程采用该目录。

本机没有原生C++源码，故于2026-10-25查询`Leuconoe/eden`固定commit `95e3fdf628bb2830ea8765ae9410ee3132996a1c`，作为版本绑定的实现参考，并保存下载哈希。Windows的[路径实现](https://github.com/Leuconoe/eden/blob/95e3fdf628bb2830ea8765ae9410ee3132996a1c/src/common/fs/path_util.cpp)从`GetModuleFileNameW`取得exe目录，优先相邻user，否则使用Known Folder；改变cwd或APPDATA环境不能据此实现隔离。[GUI初始化和参数解析](https://github.com/Leuconoe/eden/blob/95e3fdf628bb2830ea8765ae9410ee3132996a1c/src/yuzu/main_window.cpp)先创建路径、加载QtConfig，再处理游戏、全屏、用户及手柄profile等参数，没有独立配置目录选项；`-u`是模拟账户，`-input-profile`是手柄配置。此参考不单独证明本机二进制，实际路径另由本批原生日志确认。

## 实测方案与结果

每份运行入口在F盘建立`eden.exe`文件硬链接，与来源exe同一文件身份，应用物理复制为0；不使用会被SDK resolve回来源目录的exe符号链接。入口旁user通过目录联接指向D盘独立profile，languages只读复用。D盘只复制config、keys及少量可变NAND，SD／认证／业务数据从空目录开始；系统固件单独缓存一次，每份profile的Contents联接到该缓存。测试中禁用自动更新，缓存文件设只读标记，前后完整哈希一致；只读标记不等于抵抗恶意写入的权限隔离。

| 项目 | 旧执行器 | 本批原型 |
|---|---:|---:|
| 每份profile准备复制量 | 397,160,256字节 | 375,658字节 |
| 每份程序物理复制 | 55,727,104字节 | 0，硬链接 |
| 固件复制 | 每份340,737,536字节 | 一次340,737,536字节 |
| 两份profile准备时间 | 旧批未计时，不作倍数比较 | 3.040秒／2.780秒 |

每批复制量减少约99.905%。这不表示游戏启动或整个suite耗时同样减少，首次缓存准备成本单独记录。严格说并非“只需一个ini”：仍需独立SD、认证／消费状态、可变NAND、缓存和日志，否则场景会串数据。

实际使用此前封存NRO SHA-256 `99b228e5569ae113af2e589ac055f060c061db44f491eeb0061eaaae7e889096`，Eden exe SHA-256 `e81f589674c9db34d4c335427e70d3d5b93ed74bef0debf8079a2975d02c6986`。三次启动均核对MCP effective profile及原生日志ConfigDir、SDMCDir，复用既有完成帧／独立HID／输入释放门禁。

1. A首次设置：3个正常UI动作，设置成功并落盘。
2. B全新启动：仍处向导第一步，无输入，A配置字节未变化。
3. A重开：回到孩子页，设置完成状态保留，无输入。

三次停止均为0退出且Windows事件审计无对应崩溃。来源exe、全部来源config／keys／NAND／languages及共享缓存前后哈希相同。该三项是profile隔离能力探测，不等同smoke、main17或支持6项，也不证明真机/PCTL。

原始材料封存在[轻量验证目录](../../../../../device-reports/eden-lightweight/20261025/)，140文件哈希清单绑定原始调用、流、截图、日志、脚本、身份和结果。首次离线脚本误将profiles数组交给仅接受object的读取器，且共用输入核验函数不接受零输入场景；补充版修正这两个核验适配，并以B原始向导观察证明全新状态。原封存脚本、报告、图及流未改写；[v2核验脚本](../../../../../device-reports/eden-lightweight/20261025-supplement/verify_probe_v2.py)绑定原清单，完成写后读回及重复核验，原错误不记为业务失败。

核验后另行验证Windows目录联接解除不会访问目标，再清理本批两份程序入口及私有profile，保留封存证据、最终配置和一次性340.74MB固件缓存。收尾结果见[owned-cleanup-result.json](../../../../../device-reports/eden-lightweight/20261025-supplement/owned-cleanup-result.json)；未因本轮验证遗留两份完整运行副本。

## 后续接入顺序与收尾规则

正式执行器仍为复制模式。本轮交付已验证的方案和原型，不暗改其现有CLI。后续先将共享资源准备与自动收尾正式接入，再推进既有X02、X01、X04计划：

1. 以程序／固件内容哈希登记缓存和所有权，支持同卷硬链接；跨卷不满足条件时明确拒绝或显式选择一次性程序缓存，不能无声退回每批全复制。禁用更新，防止来源文件并发原地改写。
2. 给每批独立profile和入口，沿用原生路径核对及NRO身份门禁；覆盖链接错误、缓存损坏、容量不足、来源变化、并发运行和准备中断的Host失败路径。
3. 成功批次先健康退出、封存和重复离线核验，再解除本批链接、删除私有临时目录；删除后复跑核验。失败／未知保留必要现场，登记范围与复查时间，避免无限保留完整副本。共享缓存仅保留仍引用的版本，独立管理旧版本清理。
4. 正式新模式至少执行一批现有main与受影响support，绑定同一实际NRO；通过后再改默认入口。日常不因此新增Eden业务回归。

已把可复用的收尾要求写入根AGENTS及[Eden操作指南](../../../Eden自动化测试.md#临时文件与共享资源收尾)。当前工具尚无自动清理实现，规则与工具状态分别说明。

## 本批实际命令

```powershell
pwsh -NoProfile -File device-reports/eden-cleanup-audit/20261025/remove-approved.ps1
pwsh -NoProfile -File device-reports/eden-cleanup-audit/20261025/remove-approved.ps1 -Execute
python -B -X utf8 device-reports/eden-cleanup-audit/20261025/verify_approved.py
python -B -X utf8 device-reports/eden-lightweight/20261025/probe.py
python -B -X utf8 device-reports/eden-lightweight/20261025-supplement/verify_probe_v2.py
pwsh -NoProfile -File device-reports/eden-lightweight/20261025-supplement/cleanup-owned-probe.ps1
pwsh -NoProfile -File device-reports/eden-lightweight/20261025-supplement/cleanup-owned-probe.ps1 -Execute
```

这是一次性执行和审计记录，不可向原输出目录重复运行有写入的命令。原始清理脚本禁止已有删除流水时盲目重放。未修改C／NRO源码，未重新构建；原NRO产物身份已在本批重新核对，完整Eden业务套件未执行。
