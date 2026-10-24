# Eden 自动化测试（发版前）

此入口在隔离 profile 中执行真实 NRO 输入，配合进程内模拟后台、回执和持久化断言。它不证明真实 PCTL、IPC/SD 队列、Tesla、休眠、底座或生产候选资格。

分层与覆盖责任见[分层测试策略](分层测试策略.md)；逐项操作、断言和扩展场景见[Eden主流程场景](Eden主流程场景.md)。`--suite smoke`（默认）只执行原7项入口烟测；发版前核心最小集合使用`--suite main --timeout 1200`，复用7项前置后执行10项主流程旅程，共17项，不重复前置认证测试。

## 执行时机

日常基础测试不运行此入口，也不自动启动模拟器或重复稳定性批次；日常使用 Host 确定性回归和必要的统一构建／产物校验。Host 中对执行器及模拟逻辑的单元测试继续保留，默认 Eden NRO 构建／校验也保持不变，两者均不启动模拟器。

Eden 场景回归集中到发版前。固定候选 NRO／manifest 后，执行一批主流程最小集合，并补充受本次变更影响的页面、规则或恢复场景。main包含设置后和业务完成后两次关闭重开，不把能力调查时的多批稳定性复测设为每次发版的固定要求；只有明确的稳定性验证目标才另排有界批次。原始证据照常封存，离线核验重复执行，避免把“重复核验报告”误解为“重复启动整套模拟器”。

## 准备与运行

先经统一构建入口生成当前源码的 Eden NRO。C、NRO 或 package 改动的最终门禁仍为 `python tools/package_remote.py`，保留默认 Eden；`--only eden` 只用于可选迭代。

需要带 protocol 2 的定制 Eden、配置好的 profile，以及装有 `eden-mcp 0.5.0` 的外部 Python。执行器本身和 Host 回归只用标准库；不会联网安装依赖、修改全局 MCP 配置或原 profile。外部依赖可显式用 `--mcp-site` 指定。

```powershell
python -X utf8 tools/automation/eden_runner.py run --eden "<Eden目录>/eden.exe" --profile "<Eden目录>/user" --mcp-python "<MCP环境>/python.exe" --mcp-site "<MCP依赖目录>" --out "device-reports/eden-automation/<全新批次>"
python -X utf8 tools/automation/eden_runner.py verify --run "device-reports/eden-automation/<批次>"
```

主流程在相同run命令追加`--suite main --timeout 1200`，仍使用全新输出目录；不要接着在原smoke目录补写新成绩。

支持与恢复专项使用`--suite support --timeout 600`，单独创建隔离profile，依次执行首次设置、正常兑换前置、诊断导出、紧急停用、完整安装恢复和恢复后重开，共6项。它复用正常输入/回执门禁，不重复PIN错误等入口烟测；不计入main的17项。紧急停用是本地标记写入，须核对后台status已观察到该标记；完整恢复才以唯一`restore_install_snapshot`回执核对。专项要求两次健康进程退出，动作上限180，超时不重发。

Eden重开会重建进程内PCTL stub；`restored + disable.flag`必须从原安装快照重建模拟设置及timer，不能重套旧`last_enforced`额度。快照不可读或模拟恢复失败时拒绝初始化，不降级到旧规则。Host覆盖timer开/关、设置镜像一致及失败不放行；这些均不证明真实主机重启或PCTL硬件生命周期。

每次启动的Eden就绪等待最多60秒；MCP还会在创建进程前准备profile/预检，因此启动RPC的总预算为150秒，两者均受suite剩余期限约束。只到control_ready或超时仍禁止输入，不自动重启重试。

页面等待允许在原20秒内等有效身份的旧快照出现新心跳；四秒新鲜度门槛不变，没有新鲜快照不发送输入。身份错误和未来文件时间直接停止，输入前的独立检查仍保留。此等待只重读观察，不重发操作。

输出目录必须不存在；每次创建新的 `build/eden-runtimes/<随机ID>`。只复制 config、keys、nand，不复制操作者 SD/认证数据；全部存储路径重写到新 profile。敏感运行材料保留在该私有目录，不进入证据清单。固定测试规则为每周每日 120 分钟，PIN 由正常首次设置创建默认 `110`；不接受或记录家庭 PIN。

可用 `--runtime-root <本地可写目录>` 将私有 exe／profile 放在指定磁盘；报告仍由 `--out` 单独指定。此入口始终复制所选软件，不能把它描述成直接操作原安装。开始输入前同时核对 MCP 的 effective_profile_root 与实际 Eden 日志中的 ConfigDir／SDMCDir；不同则停止。本机二进制的直启会优先使用相邻user目录，不能依赖 MCP 的 EDEN_USER_DIR 实现隔离。

语言／地区／渲染器默认完整沿用来源profile，NRO保留“跟随系统”，不强制切到英文。`--time-zone-index 34` 可只在私有配置中显式固定本build的PRC时区；fixtures/profile-settings.json 记录来源值、实际值和覆盖项，profile-settings-final.json 记录退出后持久化值。启动及重开均留截图，验证中文起点时须结合日志和画面，不只看配置数值。

默认沿用 profile 的渲染器。可用 `--renderer-backend 0` 在当批私有配置中选择 OpenGL，`1` 为 Vulkan；同时取消该配置的默认值标记，原 profile 不变。fixtures/renderer.json 记录请求值、实际配置值及配置哈希，eden-log.txt 留存实际渲染器日志。只有指定后端的本批结论有效，不能据一次通过声明所有 GPU／后端稳定。2026-10-23 的本机对照见[首次执行记录](验收记录/2026-10/自动化与交互/Eden最小自动回归落地与首轮执行-20261023.md)。

所选定制build的`backend=3`日志为`OpenGL_GLASM`，与上一轮`0`不同；要沿用该模式时省略覆盖参数，不凭“都是OpenGL”改写枚举。中文可写副本复测见[中文回归与稳定性记录](验收记录/2026-10/自动化与交互/Eden中文可写副本最小回归与稳定性复测-20261023.md)。

smoke默认总期限300秒、最多180次输入；main使用1200秒预算、最多500次输入，无自动重试。smoke的首次设置 → 重开应用 → PIN取消/错误/正确 → 30分钟码成功/重复拒绝依次执行。main继续串行执行预览取消、计划保存/丢弃、四级优先级、加时/缓冲、护眼、就寝、并发限制/电视恢复、备份、偏好和业务重开。每次输入先验证运行状态与新鲜页面快照；失败阻止依赖输入。业务回执必须为新出现的同ID/type文件，超时保持未知、不换ID重发。重复码测试是有意的新预览请求，与网络重试分开。

只有所选suite全部场景、全部进程退出（smoke两次/main三次）和Windows事件审计均通过，整体才为pass。零退出码及MCP isError=false不覆盖失败启动或同PID崩溃。事件日志不可读为未知，不算通过。仅停止本次服务器创建的进程，不接管已有Eden。

只读`status`可能由自动轮询和手动Y同时产生：每份新回执均核对文件ID/type、成功状态及整数完成时间，全部留存，按完成时间取最新；同秒以ID稳定排序。此结论是新鲜只读状态，不声称某份一定来自Y。规则写入、兑换、领取等业务仍只允许唯一新回执，多份即停止，不能套用status例外。

## Eden 只读 UI 快照 v1

当前执行器要求NRO具有`input_sampling_version:1`的只读采样观察；旧NRO须先经统一入口重建，缺少能力时禁止输入。主循环与PIN模态循环只记录`input_active`布尔值和递增`input_sequence`，不记录按键掩码、坐标或PIN内容。执行器每个动作只发送一次down，最多5秒等待同实例新序号的活跃采样，然后在finally发送一次up，再最多5秒等待后续中立采样。采样不证明点中了正确控件，页面/长度/业务回执断言仍保留；没有采样或释放证据即停止，不补发down。

下一次down之前的中立样本还必须`waiting=false`，避免状态刷新正在等待回执时页面拒绝保存等按键。down后的采样与释放不受该条件限制，不能为了等回执而延迟up。此门禁最多等待5秒，不能用输入已采样替代页面接受或同ID业务回执。

普通按钮保留200毫秒最短脉冲；护眼保存600毫秒，危险A确认从已采样后再保持1400毫秒。触摸确认已采样后立即释放，避免持有到下一个页面。方向键的最短脉冲从初次down起算，避免额外等待触发自动连发。原400毫秒触摸/200毫秒按钮的定时tap路径只属于旧批次，不能用旧成功宣称新驱动已通过。Windows原子替换快照时的只读占用仍可在1秒内重读，持久占用仍停止。

Windows快照读取以FILE_SHARE_READ／WRITE／DELETE打开短时句柄，从同一读句柄取得内容及修改时间，减少读侧阻碍模拟SD删除／替换的机会。Host回归验证旧名字可在读句柄打开时删除、旧内容仍可读、句柄关闭后新文件可发布；不把Windows替换语义或模拟SD当作真实Switch SD原子性的证明。

每个逻辑动作最多一对down/up，main的500次动作预算对应最多1000次原生输入调用，释放后保留150毫秒中立间隔。短脉冲无回执的旧批次保留未知，不在原批补发；采样确认也不能证明真机短按体验。

截图RPC返回后先重新取观察基线，再最多等待20秒，要求同实例先后出现两份序号和输入序号均前进的中立、非waiting观察，才允许后续输入。`capture-readiness/`保留基线和两份原件，`capture_readiness_version:1`报告须离线核对；旧缓存中立样本不能证明截图后已恢复。该等待不持有输入，不延长down/up各5秒采样上限，也不自动重发。

模拟 PCTL 的加时读路径在实际读取时推进累计时间并采样日期／时间戳，避免把页面轮询缓存冒充新鲜加时读数；生产核心的新鲜度门禁保持不变。它仍然只是模拟累计使用量。

仅 `PLAYWISE_EDEN` 编译；由测试 profile 的 `support/automation-observe.flag` 显式启用。绘制路径最多每秒10次追加一条JSON到 `support/automation-ui.jsonl`，每个NRO实例最多追加128MiB，达到上限后停止观察发布，执行器因缺少新心跳停止。它不提交请求、不读取动作指令、不改认证或业务状态。仅供旧NRO兼容的`automation-ui.json`仍可读取；新发布不反复删除/替换观察路径，避免与Host读句柄竞争导致只剩`.write`临时文件。

Host只读取流尾最多64KiB，选择最后一条以换行结束的完整JSON；未完成尾行不接受，已完成的损坏行直接拒绝，不向前寻找能解析的旧行。健康关闭后重开前，将上一实例流原样归档到`ui-streams/`再清理私有profile中的旧流；最终流保存到`app-final/support/`。离线核验逐条读取原始流，要求每份输入采样都存在相同实例/序号及完整内容的原记录。观察发布失败不影响产品流程；真实SD原子队列仍使用原有实现，本改动不证明或改变其语义。

外壳：`schema_version:1`、内嵌 `release_id`、本次 NRO `instance`（十六进制 tick 起点）、正整数 `sequence`、十进制字符串 `tick`、`ui` 对象。instance 仅用于测试进程观察，不是主机 boot 身份。

ui 白名单：view、parent_page、overlay、setup_step、setup_active、waiting、status_loaded、error_code、selected_index、pin_length、numpad_length、numpad_purpose、code_result_failed、code_result_pending、overlay_animating，以及 targets。枚举沿 `ui_model.h`：view 0孩子/1家长/3向导，overlay 0无/3确认/4数字输入/12兑换结果/13认证错误/22 PIN。targets 的 width/height 为1280/720，坐标中心直接来自生产布局函数；提供 setup_primary、child_code、pin_confirm、pin_0..9、code_0..9。执行器不写死页面 X 偏移，并等待弹层入场结束。

不导出 PIN、加时码、配对密钥、弹窗自由文本或可复用认证材料。长度只用于等待每次独立输入处理完成；不能作为认证成功证明，仍核对进入页面、认证计数及对应业务回执。

主流程扩展同一只读schema v1的数字/布尔白名单：plan_page、overlay_selection、editor_index、weekly_dirty、confirm_hold_required、config_groups、config_backup_ready、daily_buffer_available/claimed、daily_restriction_active、dock_restriction_active、bedtime_active/skipped、eye_resting、restricted_now、unrestricted_today和today_minutes。targets另提供页签、对应页面卡片、周编辑、时长字段、备份字段、电视页及规则总开关的生产布局中心；时长键盘使用其专属生产几何。观察不接受动作指令，不赋予认证。formatter空间不足不发布截断JSON，Host测试覆盖类型、布局中心、敏感文本排除与失败路径。

支持专项增加`disable_flag_present`、`recovery_active`两个布尔值和`diagnostic_status`整数（0空闲、1导出中、2成功、3失败，来自`PtcUiDiagnosticStatus`）。支持页的`card_*`中心来自`ptc_ui_support_card_rect`，不能沿用设置页坐标；不导出诊断路径或自由文本。

采样能力位于外壳：`input_sampling_version:1`、正整数`input_sequence`、布尔`input_active`。序号饱和而不回绕；新实例由原instance区别。采样状态只来自正常HID读取，观察路径不处理命令。

## 证据与离线复核

输出冻结 NRO/manifest、当批执行器／桥接脚本及哈希、完整工具回应（图片保留原 PNG）、UI 快照、业务回执、必要规则/state/nonce、仅含计数的认证摘要、自建 PID、原生输出及同 PID Windows XML。截图在 gallery.html 集中查看；UI快照不替代可读性与实际画面核对。

summary.json 将 pass、failed_or_unknown、not_executed 分开。evidence-manifest.json 绑定所有附件。先落盘再连续核验两次；离线 verify 只说明证据完整，不把运行失败变成通过。封存后另建补充材料，不改旧 raw 或报告。

采用采样驱动的通过报告还须离线核对`actions`：每个动作恰好一对相同输入参数的down/up，并有同实例活跃采样及序号更大的中立采样。`input-samples/`保存采样原件，checkpoint的`screenshot_call`指向对应calls/PNG；不能只写一个“已释放”布尔值签署成功。旧无采样批次继续按原schema范围验证，不追补其输入证据。
