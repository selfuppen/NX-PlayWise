# 工具分类与使用入口

[开发文档入口](../docs/开发/README.md) · [当前进度](../docs/开发/当前进度与后续指导.md) · [测试操作手册](../docs/开发/测试操作手册.md)

本目录盘点于2026-10-16：57个Python／PowerShell脚本及`ptc_frontend/`的14个既有跟踪文件。分类清单同时保存在[catalog.json](catalog.json)，每个脚本恰好一项；README与catalog本身为本次新增索引。

## 日常怎么选

- Host回归：`python tools/test.py`。
- C／NRO／Overlay／产物门禁：`python tools/package_remote.py`，保留默认Eden；不要用若干内部打包脚本拼接旁路。
- 标准真机取证：先读开发手册和`pctl_standard_evidence.py`的prepare／audit边界，实际更新或同候选重启使用`auto_update.py`，需原候选、前像、正常PIN和有限租期。
- 自然午夜离线核验：`pctl_production_midnight.py`；`pctl_midnight_evidence.py`是兼容旧报告的不同接口。
- 正式资格：单独核对完整包组和报告，不把一次标准采集、旧Lab或AI观察自动当作完整资格。

命令从仓库根目录运行。单个工具支持的参数以其源码／帮助及对应指南为准；分类不构成运行、写设备或晋级的授权。凭据不放命令行、源码或日志。

## 为什么保留现有脚本路径

现有脚本均为跟踪源码，存在Makefile／测试固定调用、同级Python导入和基于`__file__`的根目录推导。低频、lab-only或旧报告兼容不等于临时垃圾；本次按用途分类，不移动可执行文件，避免破坏现有CLI、回归及封存脚本依赖。将来拆包须同时迁移调用和补回归。

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
| [auto_update.py](auto_update.py) | 正常PIN有界更新／显式同候选采集重启，禁止重放已开始run |
| [pctl_standard_evidence.py](pctl_standard_evidence.py) | 标准候选prepare、stage、只读拉回和原始审计；不授予完整资格 |
| [pctl_production_midnight.py](pctl_production_midnight.py) | 新标准raw的自然午夜离线核验；不创建时间窗口或设备采集 |
| [bounded_input_batch.py](bounded_input_batch.py) | 受限输入辅助库，限制动作／期限并释放detach，不是任意导航脚本 |

## 安装维护

可能覆盖SD安装文件；不替代正常安全交接。

| 文件 | 用途 |
| --- | --- |
| [install_package_to_sd.ps1](install_package_to_sd.ps1) | 本地SD目录安装 |
| [install_package_via_dbi_mtp.ps1](install_package_via_dbi_mtp.ps1) | DBI MTP安装 |
| [install_package_via_ftp.ps1](install_package_via_ftp.ps1) | FTP安装的PowerShell入口 |
| [install_package_via_ftp.py](install_package_via_ftp.py) | FTP安装实现与CLI |

## 构建与资源依赖

由构建／测试入口调用；保持文件位置。

| 文件 | 用途 |
| --- | --- |
| [build_ptc_standalone.py](build_ptc_standalone.py) | 离线家长网页生成 |
| [convert_ui_previews.py](convert_ui_previews.py) | PPM转PNG |
| [generate_release_manifest.py](generate_release_manifest.py) | 产物／源码／工具链身份清单 |
| [make_audio_fixtures.py](make_audio_fixtures.py) | 音效fixture生成 |
| [make_fixtures.py](make_fixtures.py) | 令牌确定性fixture生成 |
| [package_delivery.py](package_delivery.py) | 完整交付包 |
| [package_device_lab.py](package_device_lab.py) | 独立Device Lab包 |
| [package_sdmc.py](package_sdmc.py) | 标准SD目录与Zip组织 |
| [playwise_version.py](playwise_version.py) | 版本读取／校验共享库 |
| [stage_timer.py](stage_timer.py) | 构建阶段计时与报告 |
| [sync_doc_previews.py](sync_doc_previews.py) | 文档预览同步 |

## 协议共享库

被CLI、服务或测试导入；不能当临时脚本移动。

| 文件 | 用途 |
| --- | --- |
| [ptc_token_v1.py](ptc_token_v1.py) | v1令牌算法 |
| [ptc_token_v2.py](ptc_token_v2.py) | v2八位码算法 |
| [ptc_request_queue.py](ptc_request_queue.py) | 标准请求队列参考实现 |

## 正式资格

按完整候选／环境／报告要求使用，不是日常采集的替代品。

| 文件 | 用途 |
| --- | --- |
| [pctl_usage_qualification.py](pctl_usage_qualification.py) | v1／v2草稿、原始端点配对及短链核验 |
| [verify_device_qualification.py](verify_device_qualification.py) | 完整包组与报告门禁 |
| [promote_qualified_build.py](promote_qualified_build.py) | 通过正式门禁后晋级产物 |

## Device Lab与专项输入

非标准产品执行入口；明确计划和资格范围后使用。

| 文件 | 用途 |
| --- | --- |
| [assert_device_evidence.py](assert_device_evidence.py) | 标准／Lab拉回材料的机器断言 |
| [assert_selfdrive_evidence.py](assert_selfdrive_evidence.py) | Lab自驱动流断言 |
| [capability_probe.py](capability_probe.py) | Lab能力探测 |
| [capture_device_observations.py](capture_device_observations.py) | 不可自动替代观察的采集 |
| [device_lab_roundtrip.py](device_lab_roundtrip.py) | Lab证据往返 |
| [lab_wire_client.py](lab_wire_client.py) | Lab FTP wire客户端 |
| [make_bedtime_qualification_template.py](make_bedtime_qualification_template.py) | 就寝资格模板 |
| [make_eye_care_qualification_template.py](make_eye_care_qualification_template.py) | 护眼资格模板 |
| [prepare_device_lab_sd.ps1](prepare_device_lab_sd.ps1) | 准备Lab SD目录 |

## 专项通道与视觉辅助

显式专项计划使用；AI判读不能冒充本轮独立人工答复。

| 文件 | 用途 |
| --- | --- |
| [sys_botbase_client.py](sys_botbase_client.py) | socket客户端；pixelPeek截图与输入是不同能力 |
| [input_smoke_harness.py](input_smoke_harness.py) | 输入通道烟测，不直接证明业务／游戏效果 |
| [probe_report.py](probe_report.py) | 能力探测HTML报告 |
| [vision_capture_driver.py](vision_capture_driver.py) | Lab视觉采集驱动 |
| [vision_verdict.py](vision_verdict.py) | 绑定视觉判读附件，保留来源类型和适用门禁 |

## 历史探测兼容

保留原报告与回归依赖；不得作为当前标准批次默认路线。

| 文件 | 用途 |
| --- | --- |
| [package_pctl_capture.py](package_pctl_capture.py) | 内部诊断tap暂存包，改变NSP，不授予标准候选资格 |
| [package_usage_probe.py](package_usage_probe.py) | 独立使用时间实验产物 |
| [pctl_capture.py](pctl_capture.py) | 旧诊断tap暂存／激活／恢复事务 |
| [pctl_fresh_takeover.py](pctl_fresh_takeover.py) | 原新安装隔离与恢复调查，不能从任意现场一键重放 |
| [pctl_mainline_report.py](pctl_mainline_report.py) | 旧主线材料汇总 |
| [pctl_midnight_evidence.py](pctl_midnight_evidence.py) | 旧午夜报告接口，保留不归零等历史结果 |
| [pctl_next_day_usage.py](pctl_next_day_usage.py) | 旧次日实际60分钟窗口 |
| [pctl_usage_evidence.py](pctl_usage_evidence.py) | 旧PCTL主线独立计数对账 |
| [pctl_usage_scenarios.py](pctl_usage_scenarios.py) | 旧有限主线补充场景 |
| [usage_probe.py](usage_probe.py) | 旧实验驱动及当前部分工具仍导入的Device等基础设施 |
| [usage_probe_analysis.py](usage_probe_analysis.py) | 旧时间线重建，派生候选不等于事实 |
| [usage_probe_autostart.py](usage_probe_autostart.py) | 旧自启动实验协调器；已知旧BD23F1故障路径不得默认重走 |

## 产品源码和临时产物

`ptc_frontend/`是实际家长网页源码与资源，不是测试临时页。离线HTML由打包工具生成，不能因“生成文件”就删除跟踪交付内容。

本次未发现混在tools根目录的未跟踪一次性脚本。`__pycache__/`、`.pyc`是可再生且已忽略的临时缓存；可在测试结束后清理，后续Python运行会重新生成。`device-reports/`中的raw、图片、冻结包、脚本和verification索引属于证据，不能与缓存一起清理；`build/`中被冻结报告引用的文件也须先确认保留范围。

新增通用工具时同步维护catalog及本页；一次性现场脚本放`build/<批次>/`，进入封存目录后遵守只追加、不可改写原则。不要向公共工具加入固定设备run、真实PIN、历史boot或一次性按键坐标。
