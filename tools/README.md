# 工具分类与使用入口

[开发文档入口](../docs/开发/README.md) · [测试操作手册](../docs/开发/测试操作手册.md)

本目录核对于2026-10-19：60个Python／PowerShell脚本及`ptc_frontend/`的14个既有跟踪文件。分类清单同时保存在[catalog.json](catalog.json)，每个脚本恰好一项；根目录仅保留6个日常入口，其余按实际子目录管理。

## 日常怎么选

- Host回归：`python tools/test.py`。
- C／NRO／Overlay／产物门禁：`python tools/package_remote.py`，保留默认Eden；不要用若干内部打包脚本拼接旁路。
- 标准真机取证：先读开发手册和`device/pctl_standard_evidence.py`的prepare／audit边界，实际更新或同候选重启使用`device/auto_update.py`，需原候选、前像、正常PIN和有限租期。
- 自然午夜离线核验：`device/pctl_production_midnight.py`；`legacy/pctl_midnight_evidence.py`是兼容旧报告的不同接口。
- 正式资格：单独核对完整包组和报告，不把一次标准采集、旧Lab或AI观察自动当作完整资格。

命令从仓库根目录运行。单个工具支持的参数以其源码／帮助及对应指南为准；分类不构成运行、写设备或晋级的授权。凭据不放命令行、源码或日志。

## 实际目录

| 目录 | 职责 |
| --- | --- |
| `build/` | 统一构建内部步骤、版本库、资源与fixture生成 |
| `lib/` | 协议、队列及令牌共享库 |
| `device/` | 标准候选更新、取证、自然午夜核验及有界输入 |
| `install/` | SD、DBI MTP与FTP安装 |
| `qualification/` | 正式资格模板、核验与晋级 |
| `lab/` | Device Lab采集、断言和wire客户端 |
| `automation/` | 专项输入、截图、视觉判读与报告 |
| `legacy/` | 历史探测与报告兼容；不作为默认真机路线 |
| `compat/` | 封存脚本旧导入的显式兼容运行器 |
| `ptc_frontend/` | 家长网页产品源码和资源，保持原路径 |

子目录CLI支持 `python tools/<分类>/<脚本>.py`；Python调用使用完整的
`tools.<分类>.<模块>` 导入，库目录不再隐式加入搜索路径。Makefile、CI、测试和现行指南使用新路径。
历史验收记录中的命令保留原貌，阅读时对照catalog的`legacy_path`，不能据此直接重放设备操作。

封存脚本若只依赖旧的Python导入，可显式运行：

```text
python -X utf8 tools/compat/run_archived.py device-reports/<批次>/verify.py
```

运行器只适配旧模块名，复用同一模块和异常类型，不改原脚本、raw或报告。
它会执行目标脚本的原行为，并不禁止写文件或设备操作；仅用于已核对可重跑的离线核验。
旧的子进程CLI路径不自动重映射，此类脚本须用原commit源码或另建补充核验材料，不能改封存原件。

## 日常入口

按任务使用；构建和版本操作有写入。

| 文件 | 用途 |
| --- | --- |
| [test.py](test.py) | 完整Host回归 |
| [package_remote.py](package_remote.py) | 统一devkitPro构建、标准包与默认Eden |
| [grant_code.py](grant_code.py) | 显式生成测试或家长离线码，不把真实密钥写进示例 |
| [ptc_frontend_server.py](ptc_frontend_server.py) | 本地前端与队列服务 |
| [protocol_probe.py](protocol_probe.py) | 本地协议目录探测 |
| [release_version.py](release_version.py) | 版本维护，会修改版本文件／Git状态，按发布流程使用 |

## 标准真机取证

需要候选和前像；准备／审计与实际设备变更分开。

| 文件 | 用途 |
| --- | --- |
| [device/auto_update.py](device/auto_update.py) | 正常PIN有界更新／显式同候选采集重启，禁止重放已开始run；diagnose独立离线定位封存交接缺口 |
| [device/ui_receipt_observer.py](device/ui_receipt_observer.py) | 只读观察有界窗口内全部新增原请求／回执；多条有效回执与来源隔离分别判定，不注入输入 |
| [device/ui_status_trace.py](device/ui_status_trace.py) | 离线准备有期限的status来源取证配置，审计已停采客户端日志并配对原请求回执；不操作设备、不证明输入隔离 |
| [device/pctl_standard_evidence.py](device/pctl_standard_evidence.py) | 标准候选prepare、stage、只读拉回和原始审计；不授予完整资格 |
| [device/pctl_production_midnight.py](device/pctl_production_midnight.py) | 新标准raw的自然午夜离线核验；不创建时间窗口或设备采集 |
| [device/bounded_input_batch.py](device/bounded_input_batch.py) | 受限输入辅助库，限制动作／期限并释放detach，不是任意导航脚本 |
| [device/input_connection.py](device/input_connection.py) | 授权输入前可选的中立连接等待，沿用同一租期和收尾；截图不调用 |

## 安装维护

可能覆盖SD安装文件；不替代正常安全交接。

| 文件 | 用途 |
| --- | --- |
| [install/install_package_to_sd.ps1](install/install_package_to_sd.ps1) | 本地SD目录安装 |
| [install/install_package_via_dbi_mtp.ps1](install/install_package_via_dbi_mtp.ps1) | DBI MTP安装 |
| [install/install_package_via_ftp.ps1](install/install_package_via_ftp.ps1) | FTP安装的PowerShell入口 |
| [install/install_package_via_ftp.py](install/install_package_via_ftp.py) | FTP安装实现与CLI |

## 构建与资源依赖

由统一构建／测试入口调用，不单独拼接发布流程。

| 文件 | 用途 |
| --- | --- |
| [build/build_ptc_standalone.py](build/build_ptc_standalone.py) | 离线家长网页生成 |
| [build/convert_ui_previews.py](build/convert_ui_previews.py) | PPM转PNG |
| [build/generate_release_manifest.py](build/generate_release_manifest.py) | 产物／源码／工具链身份清单 |
| [build/make_audio_fixtures.py](build/make_audio_fixtures.py) | 音效fixture生成 |
| [build/make_fixtures.py](build/make_fixtures.py) | 令牌确定性fixture生成 |
| [build/package_delivery.py](build/package_delivery.py) | 完整交付包 |
| [build/package_device_lab.py](build/package_device_lab.py) | 独立Device Lab包 |
| [build/package_sdmc.py](build/package_sdmc.py) | 标准SD目录与Zip组织 |
| [build/playwise_version.py](build/playwise_version.py) | 版本读取／校验共享库 |
| [build/stage_timer.py](build/stage_timer.py) | 构建阶段计时与报告 |
| [build/sync_doc_previews.py](build/sync_doc_previews.py) | 文档预览同步 |

## 协议共享库

被CLI、服务或测试按包路径导入。

| 文件 | 用途 |
| --- | --- |
| [lib/ptc_token_v1.py](lib/ptc_token_v1.py) | v1令牌算法 |
| [lib/ptc_token_v2.py](lib/ptc_token_v2.py) | v2八位码算法 |
| [lib/ptc_request_queue.py](lib/ptc_request_queue.py) | 标准请求队列参考实现 |

## 正式资格

按完整候选／环境／报告要求使用，不是日常采集的替代品。

| 文件 | 用途 |
| --- | --- |
| [qualification/pctl_usage_qualification.py](qualification/pctl_usage_qualification.py) | v1／v2草稿、原始端点配对及短链核验 |
| [qualification/verify_device_qualification.py](qualification/verify_device_qualification.py) | 完整包组与报告门禁 |
| [qualification/promote_qualified_build.py](qualification/promote_qualified_build.py) | 通过正式门禁后晋级产物 |
| [qualification/make_bedtime_qualification_template.py](qualification/make_bedtime_qualification_template.py) | 就寝资格模板 |
| [qualification/make_eye_care_qualification_template.py](qualification/make_eye_care_qualification_template.py) | 护眼资格模板 |

## Device Lab与专项输入

非标准产品执行入口；明确计划和资格范围后使用。

| 文件 | 用途 |
| --- | --- |
| [lab/assert_device_evidence.py](lab/assert_device_evidence.py) | 标准／Lab拉回材料的机器断言 |
| [lab/assert_selfdrive_evidence.py](lab/assert_selfdrive_evidence.py) | Lab自驱动流断言 |
| [lab/capability_probe.py](lab/capability_probe.py) | Lab能力探测 |
| [lab/capture_device_observations.py](lab/capture_device_observations.py) | 不可自动替代观察的采集 |
| [lab/device_lab_roundtrip.py](lab/device_lab_roundtrip.py) | Lab证据往返 |
| [lab/lab_wire_client.py](lab/lab_wire_client.py) | Lab FTP wire客户端 |
| [lab/prepare_device_lab_sd.ps1](lab/prepare_device_lab_sd.ps1) | 准备Lab SD目录 |

## 专项通道与视觉辅助

显式专项计划使用；AI判读不能冒充本轮独立人工答复。

| 文件 | 用途 |
| --- | --- |
| [automation/sys_botbase_client.py](automation/sys_botbase_client.py) | socket客户端；pixelPeek截图与输入是不同能力 |
| [automation/input_smoke_harness.py](automation/input_smoke_harness.py) | 输入通道烟测，不直接证明业务／游戏效果 |
| [automation/probe_report.py](automation/probe_report.py) | 能力探测HTML报告 |
| [automation/vision_capture_driver.py](automation/vision_capture_driver.py) | Lab视觉采集驱动 |
| [automation/vision_verdict.py](automation/vision_verdict.py) | 绑定视觉判读附件，保留来源类型和适用门禁 |

## 历史探测兼容

保留原报告与回归依赖；不得作为当前标准批次默认路线。

| 文件 | 用途 |
| --- | --- |
| [legacy/package_pctl_capture.py](legacy/package_pctl_capture.py) | 内部诊断tap暂存包，改变NSP，不授予标准候选资格 |
| [legacy/package_usage_probe.py](legacy/package_usage_probe.py) | 独立使用时间实验产物 |
| [legacy/pctl_capture.py](legacy/pctl_capture.py) | 旧诊断tap暂存／激活／恢复事务 |
| [legacy/pctl_fresh_takeover.py](legacy/pctl_fresh_takeover.py) | 原新安装隔离与恢复调查，不能从任意现场一键重放 |
| [legacy/pctl_mainline_report.py](legacy/pctl_mainline_report.py) | 旧主线材料汇总 |
| [legacy/pctl_midnight_evidence.py](legacy/pctl_midnight_evidence.py) | 旧午夜报告接口，保留不归零等历史结果 |
| [legacy/pctl_next_day_usage.py](legacy/pctl_next_day_usage.py) | 旧次日实际60分钟窗口 |
| [legacy/pctl_usage_evidence.py](legacy/pctl_usage_evidence.py) | 旧PCTL主线独立计数对账 |
| [legacy/pctl_usage_scenarios.py](legacy/pctl_usage_scenarios.py) | 旧有限主线补充场景 |
| [legacy/usage_probe.py](legacy/usage_probe.py) | 旧实验驱动及当前部分工具仍导入的Device等基础设施 |
| [legacy/usage_probe_analysis.py](legacy/usage_probe_analysis.py) | 旧时间线重建，派生候选不等于事实 |
| [legacy/usage_probe_autostart.py](legacy/usage_probe_autostart.py) | 旧自启动实验协调器；已知旧BD23F1故障路径不得默认重走 |

## Eden 自动化（发版前）

| 文件 | 用途 |
| --- | --- |
| [automation/eden_runner.py](automation/eden_runner.py) | 发版前隔离首次设置、PIN、加时码业务烟测，关联Windows事件并离线核验证据 |
| [automation/eden_main_flow.py](automation/eden_main_flow.py) | 发版前中文业务旅程串行编排与离线断言复核 |
| [automation/eden_support_flow.py](automation/eden_support_flow.py) | 支持恢复专项：诊断、停用、安装恢复与重开，保留正常业务痕迹 |
| [automation/eden_mcp_bridge.py](automation/eden_mcp_bridge.py) | 在显式指定的外部Python/MCP环境启动定制Eden，记录自建PID |

准备、命令、只读UI快照与证明边界见[开发说明](../docs/开发/Eden自动化测试.md)。日常基础测试不启动此执行器；Host单元测试和默认Eden NRO构建／产物校验继续保留，均不启动模拟器。这些工具不连接真实Switch。

## 产品源码和临时产物

`ptc_frontend/`是实际家长网页源码与资源，不是测试临时页。离线HTML由打包工具生成，不能因“生成文件”就删除跟踪交付内容。

本次未发现混在tools根目录的未跟踪一次性脚本。`__pycache__/`、`.pyc`是可再生且已忽略的临时缓存；可在测试结束后清理，后续Python运行会重新生成。`device-reports/`中的raw、图片、冻结包、脚本和verification索引属于证据，不能与缓存一起清理；`build/`中被冻结报告引用的文件也须先确认保留范围。

新增通用工具放入对应分类，并同步维护catalog及本页；一次性现场脚本放`build/<批次>/`，进入封存目录后遵守只追加、不可改写原则。不要向公共工具加入固定设备run、真实PIN、历史boot或一次性按键坐标。

## 封存脚本兼容

| 文件 | 用途 |
| --- | --- |
| [compat/run_archived.py](compat/run_archived.py) | 显式运行已核对的离线封存脚本，保留旧导入；不自动迁移旧CLI |
