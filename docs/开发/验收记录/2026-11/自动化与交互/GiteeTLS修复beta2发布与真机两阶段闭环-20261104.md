# Gitee TLS 修复 beta.2 发布与真机两阶段闭环

2026-11-04，Asia/Shanghai。接续[候选审核材料](GiteeTLS修复beta2候选与审核材料-20261104.md)。用户独立授权发布冻结 beta.2 并继续真机验证后，GitHub 预发行版与 Gitee 镜像发布完成；两次真实在线更新及阶段间受支持的真实程序基线恢复均通过。原规则、完整 PCTL 设置、计时状态和全部偏好恢复，正常使用量保留，两份标准采集停止；用户集中确认 13 图“画面均符合标题”。这是本次在线更新范围的闭环，完整设备资格仍为 `false`。

## 冻结发布身份

- 源码：`0298dbeaf65bc4f42874c35c9b0442567521c61d`，clean，版本 `3.1.3-beta.2`。
- release ID：`playwise-3.1.3-beta.2+0298dbeaf65b.a4777004589501c2`。
- 标准包：3,812,857 字节，SHA-256 `2a6f44e70834507650933b9c81756e64acfcb571954cc2ee0cfbc5fcdf1c9b10`。
- 完整包：3,822,269 字节，SHA-256 `c792fc74f97069ef308d8dcd7c9665809eced51899605b7bb1a1eca94247ce13`。
- [GitHub 预发行版](https://github.com/selfuppen/NX-PlayWise/releases/tag/v3.1.3-beta.2)，release ID `433425434`，tag 指向上述冻结源码，`prerelease=true`、`latest=false`；[Gitee 镜像](https://gitee.com/selfuppen/nx-playwise/releases/tag/v3.1.3-beta.2)。仅发布审核过的标准、完整两个附件。

镜像工作流 `41757597632` 首轮成功。两端 feed commit 同为 `00e406dc047a7bf50e2c9dd83c9f1be6f0f11980`，稳定版保持 `v3.0.2`、preview 为 `v3.1.3-beta.2`。两附件在两来源各两次真实匿名下载，共八次，大小、SHA-256、MD5 和镜像状态一致。授权原文、草稿 ID 读回、发布回执及下载原件位于 `device-reports/gitee-beta2-publication-20261104-01/`。原候选审核封存保持发布前的时间点记录，未回写其中“待授权”字段。

软件门禁沿用同一冻结候选的完整冷构建、最终 clean 构建、CI `41753835222`、默认 Eden NRO 构建及产物校验。实际普通 Eden 场景为 main 17/17、support 6/6，所属进程健康退出、证据封存及临时目录清理已通过，详见前述候选记录；本次硬件收尾只增加记录，没有重编译、重新跑 Eden 或制造新候选。

## 两阶段与真实基线恢复

设备前检、采集和正常家长认证均绑定各轮新现场；未使用官方临时 PIN 解锁。每个临时不限时窗口在控制前启动独立守护，预先保存一次恢复请求和最终状态请求，并设置 15 分钟恢复边界及额外 2 分钟收尾上限。实际均提前正常结束。

| 步骤 | 源身份 → 实际目标身份 | 结果 |
| --- | --- | --- |
| 第一阶段：真实下载仅作准备，验证安装链 | clean 6caf，PID 156／boot `fb74a1b0a6cfe1b2` → beta.2，PID 158／boot `a3fb28cb76dfed95` | 独立配置备份、13 文件安装、正常重开、PIN 确认、安全交接及 active 通过 |
| 阶段间程序基线恢复 | beta.2 PID 158 → 原真实 clean 6caf，PID 160／boot `035f6cc1ba746b85` | 受支持协调器整包发布、正常 PIN 及交接，`ready_for_tests`，正式离线 report 重复通过 |
| 第二阶段：完整在线链路 | clean 6caf PID 160 → beta.2，PID 163／boot `0b0bf0e7919a6378` | 打开时自动检查、正式／预发行通道、手动检查、真实下载、备份、安装、重开和交接通过 |

阶段间恢复包是原冻结 `playwise-3.1.3-beta.1+6caf149b471e.c8b9ed9aa65aeb92`，SHA-256 `112463a30140bc23d9c5ee5bbd5375f695ee035b75803271fb88290d4bb89607`，不是公开 beta.1 的 81d 附件。旧包按其自己的冻结版本完整校验，13 个程序文件均回读匹配。只恢复程序，不覆盖旧 state、nonce、账本或活动，也没有伪造版本／release ID、绕过同候选门禁、强杀后台或整机重启。

两次在线下载均来自真实 Gitee HTTPS，原生 libnx TLS 的固定 CA 导入成功、HTTP 200，收到 3,812,857 字节，ZIP SHA-256 与批准标准包一致，MD5 均为 `c484c174c517fbf6fea04f46a10f20b1`。没有使用本地 ZIP 入口或协调器离线发布替代在线安装器。

| 原事务 | 第一阶段 | 第二阶段 |
| --- | --- | --- |
| transaction ID | `a999d8c8bc0d9d82ce6fd48f80c3a003` | `1d1bc3c061a4e28a65e486bfaede726c` |
| 原配置备份请求 | `1793763890007-82af` | `1793765245003-b134` |
| 目标核验 status 请求 | `1793764140004-8677` | `1793765400004-828a` |
| 独立配置备份 SHA-256 | `cc9cb0dad0012aa73136c48ad86fb85944511e1902cf1231cab22cdaa5ac4482` | `9adbca006c67430686460893e5edfa8e7498d7af8f0d48e56c654b28161a9bad` |

每份备份 3,606 字节，原 `create_config_backup`（请求类型 49）accepted／receipt 同 ID/type 且成功。安装器先将配置归档、经独立路径解析读回，再开始安装。宿主另行拉回归档并核验文件清单、每项内容哈希、journal 的 config 大小／哈希，以及 13 文件的 before、stage、最终 live 字节。两份 journal 均为 `completed`、`next_file=13`，source／target PID、boot、release 和原 status 回执哈希均匹配，install.lock 已解除。

第二阶段启用“打开时检查”并正常退出、重开相同 forwarder，保持仅正式版；取得新观测 `d767150929d28cdc6719f03804e98bac`，与重开前观测不同，状态正确显示本地版本高于稳定版 3.0.2。随后正常关闭启动检查、开放预发行并手动检查，取得另一观测 `a506d61b37c49567becf14e4393bfcac`，识别 beta.2 可更新。原始网络报告先落盘，再核验 schema、严格类型、实际 NRO 和运行身份；没有把留存报告当成本次检查。

## 恢复、停采与业务痕迹

三次已激活窗口的恢复请求均只发布一次，原同 ID/type 回执成功：第一阶段 `06669ab3e79541338edd1e96f6750bce`，阶段间 `a37f2c981de242d3a0d65c39b660c8a2`，第二阶段 `9cc3c470290a4332a7f8311686746a13`。三轮原规则字节／有效语义一致，完整 0x44 前像恢复、timer 回到原 0，非官方临时解锁，独立守护均退出 0。

同日实际 1952 累计依次为 `3610 → 4132 → 4283 → 4789` 秒，带符号余额没有钳制或回退。最终每日 60 分钟规则生效、额度耗尽；测试中自然累计的用量保留。源有“启动检查关、仅正式版”的更新偏好和全部其他配置均恢复。

两份标准采集均绑定实际冻结生产组件、新 target boot 和独立 run。第一阶段 22 样本，raw SHA-256 `786dac284ed1a665c810708eea4d4b59937723a619fa690332078e4e738104c6`；第二阶段 17 样本，raw SHA-256 `2257e6f044e29cf94e5087e58e908cec065953f9ef04039ab623516d8e55d5ce`。配置只在目标启动前写入，及时恢复后归档本批配置；各三次完整拉回字节一致，均有 stop 记录，无缺行、坏样本或间隙。无活动采集、未知请求、恢复标记或新 fatal/crash。

认证身份、设备凭据和缓冲领取字段另行核验。备份格式按 `platform/config_backup.c:313–326` 仅导出 PIN 的 version／算法／hash／salt，不导出冷却和时间字段；这四项在两归档和最终设备间相同，凭据原字节相同，当前失败次数和冷却均为零，认证更新时间早于本批。没有用备份回写业务 state。nonce 账本未安排完整前像比较，因此不宣称其前后逐字节相等；本批没有加时代码／nonce 消耗操作，也未还原账本或活动。

## 原异常保留与画面复核

- 第一阶段最初会话在隐藏认证输入前已结束；无 window plan、无政策窗口，原入口和中止记录保留，新前检后另建批次。
- 第一阶段元信息初次宿主解析误用 `version` 而非 `schema_version`，raw 已先保存，解析失败阻断后续导航；补充绑定保留该错误，未改 raw。
- 第一阶段重开导航曾错误推断按钮选择位置；正常 B 返回孩子区、退出至已核实同 forwarder，再正常打开成功。不能把这次导航假设当作产品“退出并重开”故障。
- 第一阶段恢复偏好导航误切音效一次，已按原开启值恢复，完整 config JSON 与前像一致。
- 第二阶段第一批在 Y 刷新／移动后重复截图超时，输入事件和已有原图保留；尚未提交不限时请求，守护 `window_not_activated`、退出 0。新前检无健康变化、队列无未知，另批改为每组一次 pixelPeek，截图不可用单独记录，身份／请求／恢复门禁保持。
- 最终补充审计误将过滤后的 auth 备份与完整 live auth 做原字节比较，未产出通过结论、未写设备。原比较停止记录保留；独立 `business-preservation-02` 按实际备份契约重推导并重复通过，未回写原 raw。

两次安装、PIN 后加载确认、新版核验，以及恢复后的系统原生限制、beta.2 Overlay“今日时间已用完／今日额度60分钟”和正常关闭 Overlay，共 13 图集中在 `gitee-beta2-two-stage-result-20261104-01/gallery.html`。截图均为 pixelPeek；管理器显示的时间不是设备计时证据。用户统一确认原话“画面均符合标题”，`human-observation.json` 独立绑定问题 ID、画廊索引、全部原图和机器附件哈希。没有逐图索取或用机器读数替代人工结论。

## 封存、适用范围与重跑

最终结果位于 `device-reports/gitee-beta2-two-stage-result-20261104-01/`。`seal.json` 绑定 1,153 项输入，包含实际任务脚本、两次完整在线事务、真实基线恢复、三轮规则恢复、失败／补充审计、停采 raw、原图和用户确认。JSON 读回后从落盘输入重新推导，独立进程重复离线核验。旧候选和旧失败封存未改写；记录提交与冻结生产源码不同，不触发重新构建／部署。

本批 Gitee 主路径未失败，GitHub 备用未实际触发；没有已证明安全且受支持的故障注入前提，未更改设备网络或关闭 TLS 来制造结果。此前 GitHub 原生握手失败不因本次 Gitee 成功而宣称修复。实际游戏效果、POWER 真休眠、底座及一般生产完整资格不在此安装批次结论中，`qualified:false` 保持。

主要实际命令（认证信息只经隐藏交互驻留内存）：

```text
python -u -B -X utf8 build/publish-beta2-20261104.py
python -u -B -X utf8 build/verify-beta2-publication-20261104.py
python -u -B -X utf8 build/beta2-stage1-console-20261104-02.py
python -u -B -X utf8 build/beta2-baseline-console-20261104.py
python -u -B -X utf8 build/beta2-stage2-console-20261104-02.py
python -u -B -X utf8 build/beta2-stage1-closeout-20261104.py --verify
python -u -B -X utf8 build/beta2-stage2-closeout-20261104.py --verify
python -u -B -X utf8 build/beta2-business-preservation-20261104.py --verify
python -u -B -X utf8 device-reports/gitee-beta2-two-stage-result-20261104-01/verify.py
```

最终 `verify.py` 只读，无设备连接或业务重放；它使用各原脚本的冻结字节并提供原工作区执行路径上下文，重核两次事务、阶段间正式 report、业务字段、采集 raw 和人工附件绑定。
