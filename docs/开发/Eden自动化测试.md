# Eden 自动化测试（发版前）

此入口在隔离 profile 中执行真实 NRO 输入，配合进程内模拟后台、回执和持久化断言。它不证明真实 PCTL、IPC/SD 队列、Tesla、休眠、底座或生产候选资格。Eden 当前没有 CFW/sysmodule 层，`.ovl` 不能作为浮窗加载（实测与其 VI/HID 缺口见[Eden与Tesla浮窗加载能力调研](../调研/Eden与Tesla浮窗加载能力调研.md)）。

分层与覆盖责任见[分层测试策略](分层测试策略.md)；逐项操作、断言和扩展场景见[Eden主流程场景](Eden主流程场景.md)。`--suite smoke`（默认）只执行原7项入口烟测；发版前核心最小集合使用`--suite main --timeout 1200`，复用7项前置后执行10项主流程旅程，共17项，不重复前置认证测试。

## 执行时机

日常基础测试不运行此入口，也不自动启动模拟器或重复稳定性批次；日常使用 Host 确定性回归和必要的统一构建／产物校验。Host 中对执行器及模拟逻辑的单元测试继续保留，默认 Eden NRO 构建／校验也保持不变，两者均不启动模拟器。

Eden 场景回归集中到发版前。固定候选 NRO／manifest 后，执行一批主流程最小集合，并补充受本次变更影响的页面、规则或恢复场景。main包含设置后和业务完成后两次关闭重开，不把能力调查时的多批稳定性复测设为每次发版的固定要求；只有明确的稳定性验证目标才另排有界批次。原始证据照常封存，离线核验重复执行，避免把“重复核验报告”误解为“重复启动整套模拟器”。

## 准备与运行

先经统一构建入口生成当前源码的 Eden NRO。C、NRO 或 package 改动的最终门禁仍为 `python tools/package_remote.py`，保留默认 Eden；`--only eden` 只用于可选迭代。

需要带 protocol 2 的定制 Eden、配置好的 profile，以及装有 `eden-mcp 0.5.0` 的外部 Python。执行器本身和 Host 回归只用标准库；不会联网安装依赖、修改全局 MCP 配置或原 profile。外部依赖可显式用 `--mcp-site` 指定。

shared原生运行要求Windows、NTFS文件硬链接／目录联接和PowerShell 7（`pwsh`）。Host的纯Python边界仍跨平台；Windows链接／清理fixture仅在具备该能力时运行，并在测试进程中替换进程快照查询，不依赖或操作真实Eden进程。

```powershell
python -X utf8 tools/automation/eden_runner.py run --runtime-mode shared --eden "<Eden目录>/eden.exe" --profile "<Eden目录>/user" --mcp-python "<MCP环境>/python.exe" --mcp-site "<MCP依赖目录>" --runtime-root "<数据盘>/PlayWise-Eden/runs" --application-root "<Eden同卷>/PlayWise-Eden-AppLinks/managed" --firmware-cache "<数据盘>/PlayWise-Eden/shared-resources/firmware/Contents" --out "device-reports/eden-automation/<全新批次>"
python -X utf8 tools/automation/eden_runner.py verify --run "device-reports/eden-automation/<批次>"
```

主流程在相同run命令追加`--suite main --timeout 1200`，仍使用全新输出目录；不要接着在原smoke目录补写新成绩。

支持与恢复专项使用`--suite support --timeout 600`，单独创建隔离profile，依次执行首次设置、正常兑换前置、诊断导出、紧急停用、完整安装恢复和恢复后重开，共6项。它复用正常输入/回执门禁，不重复PIN错误等入口烟测；不计入main的17项。紧急停用是本地标记写入，须核对后台status已观察到该标记；完整恢复才以唯一`restore_install_snapshot`回执核对。专项要求两次健康进程退出，动作上限180，超时不重发。

Eden重开会重建进程内PCTL stub；`restored + disable.flag`必须从原安装快照重建模拟设置及timer，不能重套旧`last_enforced`额度。快照不可读或模拟恢复失败时拒绝初始化，不降级到旧规则。Host覆盖timer开/关、设置镜像一致及失败不放行；这些均不证明真实主机重启或PCTL硬件生命周期。

每次启动的Eden就绪等待最多60秒；MCP还会在创建进程前准备profile/预检，因此启动RPC的总预算为150秒，两者均受suite剩余期限约束。只到control_ready或超时仍禁止输入，不自动重启重试。

页面等待允许在原20秒内等有效身份的旧快照出现新心跳；四秒新鲜度门槛不变，没有新鲜快照不发送输入。身份错误和未来文件时间直接停止，输入前的独立检查仍保留。此等待只重读观察，不重发操作。

输出目录必须不存在；每次在runtime-root创建随机ID私有profile，config、keys与可变NAND独立复制，不复制操作者SD／认证数据或已安装游戏。全部可写存储路径重写到本批profile，固定规则为每周每日120分钟，PIN由正常首次设置创建默认`110`，不接受或记录家庭PIN。默认`--runtime-mode shared`；`--runtime-mode copy`仅保留旧整份复制行为，须显式选择且不自动清理。

`--runtime-root`选择私有profile数据盘，建议容量充足的D盘；报告仍由`--out`指定。`--application-root`必须与源exe同卷，用NTFS文件硬链接创建本批程序入口，应用物理复制为0；入口旁user目录联接到私有profile。省略时使用源exe卷的`PlayWise-Eden-AppLinks/managed`。`--firmware-cache`是独立共享Contents目录：不存在时只复制一次并核验，已存在时必须逐文件匹配来源，否则拒绝而不覆盖；省略时为runtime-root相邻`shared-resources/firmware/Contents`。不同固件版本使用不同缓存路径。languages只读复用，密钥／可变NAND／SD／日志／缓存仍按批隔离。

开始输入前同时核对MCP的effective_profile_root与原生日志ConfigDir／SDMCDir，路径不符则停止。本机原生版优先使用相邻user，不能靠EDEN_USER_DIR或切换cwd实现隔离，也没有通用的独立ini启动参数。原型依据见[轻量profile能力验证](验收记录/2026-10/自动化与交互/Eden临时文件清理与轻量Profile验证-20261025.md)；正式执行器批次见[共享资源与X02记录](验收记录/2026-10/自动化与交互/Eden共享资源执行器与X02网页闭环-20261025.md)。每批保存runtime-ownership.json和runtime-prepared.json，分别绑定所有权／来源资源哈希，以及实际复制量／准备时间；同时持有application-root下独占租约，已有租约或已有Eden进程则拒绝准备，不接管或自动杀进程。

## 临时文件与共享资源收尾

每批开始前登记临时目录、共享资源身份及容量预算。成功批次在所属进程健康退出后，先保存NRO／构建身份、配置与数据前提、输入／回执／原始流、必要截图和日志，封存哈希清单，再从已写JSON读回并重复离线核验；核验必须能在删除运行目录后独立完成。随后清理本批临时运行文件，另存删除清单与结果，不回写原封存材料。后续测试计划只存于版本化文档或独立记录，不能只留在待删除profile中。

失败或未知批次先保留诊断所需现场，记录原因、范围和复查时间；有完整证据后优先移除可重建且与保留来源一致的资源，不再为每批永久保留或压缩一整份固件／程序。共享资源按内容哈希固定，记录引用关系；无人使用且确实由测试管理的旧缓存才纳入单独清理。来源安装、来源profile及密钥／固件原件、MCP依赖、已有原始报告不随运行目录清除。

shared模式在业务和健康收尾成功后验证来源／共享缓存未变，封存并重复核验，再调用PowerShell清理。本批联接全部核对目标后单独解除，只删除UUID绑定的程序入口和私有profile根；禁止递归穿越共享资源。清理后再重复核验报告，结果保存到`<原out>-cleanup/`，绑定原始证据清单哈希，不改写已封存报告。该目录单独封存哈希清单，并读回、重复核对JSON键／类型与来源绑定；`verify --run`同时核验已有的清理清单。失败／未知批次保留目录并登记24小时内复查时间，健康停进程后释放本批租约；若清理中断，保留追加流水，不盲目重放。业务pass和清理失败须分别记录，CLI仍返回失败。

硬链接共享文件内容，执行器在私有配置中禁用更新，固定程序哈希并检查来源完整性；测试期间不得外部更新或原地修改来源exe。跨卷不能建文件硬链接，不会静默退回全复制；容量统计不把硬链接逻辑大小算成重复物理复制。缓存仅核验和读用，不在每批结束时删除；旧缓存和历史失败现场按独立清单复核后清理。copy兼容模式仍需独立收尾，不能据shared实现声明所有旧批次会自动清理。

## X02网页配对与发码扩展

选择`--suite web --timeout 600`，使用全新profile和报告目录，执行5项：首次设置、NRO正常PIN配对／导出、真实浏览器导入／发码、NRO兑换、重开持久化。使用固定Eden测试资料，不接触真实家庭配置。网页源码和浏览器驱动封存在本批artifacts，环回HTTP只服务该冻结网页与测试导出；不把Python或CLI生成的码作为网页结果。

执行到发码阶段会保存`browser-request.json`并输出`browser_required`，最多等待180秒。读取该JSON的origin，将本批`artifacts/eden_web_browser.js`中的唯一`__EDEN_ORIGIN__`替换为该origin，通过Playwright代码执行工具运行（本环境为`browser_run_code_unsafe`）。驱动新建独立browser context，通过文件选择／确认导入和普通发码表单操作，验证重载后配对／nonce仍在，上传必要截图与一次JSON结果后关闭context。单独启动CLI不会自行启动浏览器，须由外部工具在期限内驱动；无浏览器结果、绑定错误或超时则禁止NRO码输入，不重发或自动重启。

浏览器码来自DOM，日期取NRO新鲜status的day_index；核对实际导出设备、30分钟面额、浏览器nonce、NRO预览／成功回执、活动、120→150额度和重开后的同一nonce。浏览器存储是本批真实Chromium上下文，非全局浏览器资料；手机摄像头光学扫码、PWA安装和断网能力不在本项范围。`web`不计入main17；浏览器和两次NRO退出均须通过，结束后沿用shared收尾。

## 配置与执行边界

语言／地区／渲染器默认完整沿用来源profile，NRO保留“跟随系统”，不强制切到英文。`--time-zone-index 34` 可只在私有配置中显式固定本build的PRC时区；fixtures/profile-settings.json 记录来源值、实际值和覆盖项，profile-settings-final.json 记录退出后持久化值。启动及重开均留截图，验证中文起点时须结合日志和画面，不只看配置数值。

默认沿用 profile 的渲染器。可用 `--renderer-backend 0` 在当批私有配置中选择 OpenGL，`1` 为 Vulkan；同时取消该配置的默认值标记，原 profile 不变。fixtures/renderer.json 记录请求值、实际配置值及配置哈希，eden-log.txt 留存实际渲染器日志。只有指定后端的本批结论有效，不能据一次通过声明所有 GPU／后端稳定。2026-10-23 的本机对照见[首次执行记录](验收记录/2026-10/自动化与交互/Eden最小自动回归落地与首轮执行-20261023.md)。

所选定制build的`backend=3`日志为`OpenGL_GLASM`，与上一轮`0`不同；要沿用该模式时省略覆盖参数，不凭“都是OpenGL”改写枚举。中文可写副本复测见[中文回归与稳定性记录](验收记录/2026-10/自动化与交互/Eden中文可写副本最小回归与稳定性复测-20261023.md)。

smoke默认总期限300秒、最多180次输入；main使用1200秒预算、最多500次输入，无自动重试。smoke的首次设置 → 重开应用 → PIN取消/错误/正确 → 30分钟码成功/重复拒绝依次执行。main继续串行执行预览取消、计划保存/丢弃、四级优先级、加时/缓冲、护眼、就寝、并发限制/电视恢复、备份、偏好和业务重开。每次输入先验证运行状态与新鲜页面快照；失败阻止依赖输入。业务回执必须为新出现的同ID/type文件，超时保持未知、不换ID重发。重复码测试是有意的新预览请求，与网络重试分开。

只有所选suite全部场景、全部进程退出（smoke两次/main三次）和Windows事件审计均通过，整体才为pass。零退出码及MCP isError=false不覆盖失败启动或同PID崩溃。事件日志不可读为未知，不算通过。仅停止本次服务器创建的进程，不接管已有Eden。

只读`status`可能由自动轮询和手动Y同时产生：每份新回执均核对文件ID/type、成功状态及整数完成时间，全部留存，按完成时间取最新；同秒以ID稳定排序。此结论是新鲜只读状态，不声称某份一定来自Y。规则写入、兑换、领取等业务仍只允许唯一新回执，多份即停止，不能套用status例外。

## Eden 只读 UI 快照 v1

当前执行器要求NRO具有`input_sampling_version:2`的只读采样观察；旧NRO须先经统一入口重建，缺少能力时禁止输入。主循环与PIN模态循环在正常HID读取后、分发和绘制前独立发布`support/automation-input.jsonl`：只记录`input_active`布尔值、递增`input_sequence`及实例/时间/版本，不记录按键掩码、坐标或PIN内容。边沿立即发布，中立心跳最多每200毫秒一条，每实例上限16MiB，追加累计失败3次即停；下游因缺少新采样停止。执行器每个动作只发送一次down，最多5秒等待同实例新序号的活跃采样，然后在finally发送一次up，再最多5秒等待后续中立采样。采样不证明点中了正确控件，页面/长度/业务回执断言仍保留；没有采样或释放证据即停止，不补发down。

下一次down之前的完成帧必须`waiting=false`，避免状态刷新正在等待回执时页面拒绝保存等按键。down后的采样与释放只依赖独立HID流，不等同步请求、绘制或帧提交返回，不能为了等回执而延迟up。此门禁最多等待5秒，不能用输入已采样替代页面接受或同ID业务回执。

新执行器还要求`presentation_version:1`：观察只在`framebufferEnd`返回后发布，表示该帧已完成软件绘制和提交，不代表真实显示器或原生阻断效果。每次down前，最多5秒内取得同实例的一个中立基线及两份`sequence/input_sequence`均前进的中立、非waiting、非动画帧；等待中遇到活跃输入、waiting或动画会重新建立基线，不接受缓存不变的中立记录。`input-readiness/`保留每个动作三份原件，`input_readiness_version:1`的成功报告按动作数、字段类型和原始流逐份核对。旧NRO证据仍可离线核验，但缺少完成帧标记时不能用于新的实际输入。

场景失败后至多调用一次只读`eden_get_runtime_metrics(sample_ms=500)`，保存帧计数和可用运行状态；指标不可用也独立保存错误，不替换原失败、不重发输入、不妨碍退出。单批成功不自动解释历史采样中断，完整UI、业务、原始输入流与健康收尾仍分别断言。

需要区分阻塞阶段时显式加`--trace-progress`，只在本批私有profile写`automation-trace.flag`。Eden专用`automation-progress.jsonl`记录数字阶段：1输入采样、2输入分发结束、3/4模拟tick进入/返回、5/6结果轮询进入/返回、7绘制进入、8取得framebuffer、9内容绘制完成、16提交帧之前、10提交返回、11/12观察追加进入/返回、13/14同步模拟请求进入/返回、15观察格式化失败。除阶段、结果、instance/tick与输入序号/活跃布尔之外不记录自由文本、按键、坐标或凭据。只记录输入边沿附近少数帧、每秒心跳及请求边界，每实例最多16MiB，追加累计失败3次即停止阶段取证。原始阶段流按实例归档并核验序号，不将缺少返回行直接等同已确定根因；该诊断会增加少量I/O，修复后仍须关闭它进行正常专项回归。

复制profile前，执行器统计本批实际复制的Eden可执行文件、config/keys/nand和语言资源大小，要求目标卷还可额外保留512MiB。容量不足时在创建新运行profile或启动MCP之前拒绝，保存`preparation.json`并将场景全部记为未执行；不会清理来源profile来腾空间。可用`--runtime-root`选择有足够空间的独立目录，实际路径与复制前容量绑定在`identity.json`中。容量检查通过不代表后续原生启动或业务测试通过。

普通按钮保留200毫秒最短脉冲；护眼保存600毫秒，危险A确认从已采样后再保持1400毫秒。触摸确认已采样后立即释放，避免持有到下一个页面。方向键的最短脉冲从初次down起算，避免额外等待触发自动连发。原400毫秒触摸/200毫秒按钮的定时tap路径只属于旧批次，不能用旧成功宣称新驱动已通过。Windows原子替换快照时的只读占用仍可在1秒内重读，持久占用仍停止。

Windows快照读取以FILE_SHARE_READ／WRITE／DELETE打开短时句柄，从同一读句柄取得内容及修改时间，减少读侧阻碍模拟SD删除／替换的机会。Host回归验证旧名字可在读句柄打开时删除、旧内容仍可读、句柄关闭后新文件可发布；不把Windows替换语义或模拟SD当作真实Switch SD原子性的证明。

每个逻辑动作最多一对down/up，main的500次动作预算对应最多1000次原生输入调用，释放后保留150毫秒中立间隔。短脉冲无回执的旧批次保留未知，不在原批补发；采样确认也不能证明真机短按体验。

截图RPC返回后先重新取观察基线，再最多等待20秒，要求同实例先后出现两份序号和输入序号均前进的中立、非waiting观察，才允许后续输入。`capture-readiness/`保留基线和两份原件，`capture_readiness_version:1`报告须离线核对；旧缓存中立样本不能证明截图后已恢复。该等待不持有输入，不延长down/up各5秒采样上限，也不自动重发。

模拟 PCTL 的加时读路径在实际读取时推进累计时间并采样日期／时间戳，避免把页面轮询缓存冒充新鲜加时读数；生产核心的新鲜度门禁保持不变。它仍然只是模拟累计使用量。

Eden的只读status仍同步完成；其他请求复制到一个自有待处理槽（沿用4096字节协议上限），正常HID采到中立后才在模拟tick执行一次核心。排队或执行中拒绝覆盖，执行结束即清除槽，缺少结果也不自动重放；提交OK只表示接收，成功仍须同ID回执。该槽仅属模拟器进程内适配，不是持久SD队列，也不证明真实IPC的并发或崩溃语义。

主循环在HID分发前仅处理当前请求ID已经落盘的结果，并同步命中测试的waiting标志，避免已完成的status仍等待下一次250毫秒轮询而吞掉输入。回执缺失时不提前调用结果轮询、不加速超时、不改ID或重发；成功/失败仍走原`poll_result`的校验与处理。生产端保持原轮询和等待门禁，这项适配不证明真实IPC时序。

仅 `PLAYWISE_EDEN` 编译；由测试 profile 的 `support/automation-observe.flag` 显式启用。绘制路径最多每秒10次追加一条JSON到 `support/automation-ui.jsonl`，每个NRO实例最多追加128MiB，达到上限后停止观察发布，执行器因缺少新心跳停止。它不提交请求、不读取动作指令、不改认证或业务状态。仅供旧NRO兼容的`automation-ui.json`仍可读取；新发布不反复删除/替换观察路径，避免与Host读句柄竞争导致只剩`.write`临时文件。

Host只读取流尾最多64KiB，选择最后一条以换行结束的完整JSON；未完成尾行不接受，已完成的损坏行直接拒绝，不向前寻找能解析的旧行。健康关闭后重开前，将上一实例UI/HID流原样归档到`ui-streams/`、`input-streams/`再清理私有profile中的旧流；最终流保存到`app-final/support/`。离线核验逐条读取原始流，要求每份输入采样和就绪帧都存在相同实例/序号及完整内容的原记录；UI与HID序号属于不同流，不混用。观察发布失败不影响产品流程；真实SD原子队列仍使用原有实现，本改动不证明或改变其语义。

外壳：`schema_version:1`、整数`presentation_version:1`、内嵌 `release_id`、本次 NRO `instance`（十六进制 tick 起点）、正整数 `sequence`、十进制字符串 `tick`、`ui` 对象。instance 仅用于测试进程观察，不是主机 boot 身份。

ui 白名单：view、parent_page、overlay、setup_step、setup_active、waiting、status_loaded、error_code、selected_index、pin_length、numpad_length、numpad_purpose、code_result_failed、code_result_pending、overlay_animating，以及 targets。枚举沿 `ui_model.h`：view 0孩子/1家长/3向导，overlay 0无/3确认/4数字输入/12兑换结果/13认证错误/22 PIN。targets 的 width/height 为1280/720，坐标中心直接来自生产布局函数；提供 setup_primary、child_code、pin_confirm、pin_0..9、code_0..9。执行器不写死页面 X 偏移，并等待弹层入场结束。

不导出 PIN、加时码、配对密钥、弹窗自由文本或可复用认证材料。长度只用于等待每次独立输入处理完成；不能作为认证成功证明，仍核对进入页面、认证计数及对应业务回执。

主流程扩展同一只读schema v1的数字/布尔白名单：plan_page、overlay_selection、editor_index、weekly_dirty、confirm_hold_required、config_groups、config_backup_ready、daily_buffer_available/claimed、daily_restriction_active、dock_restriction_active、bedtime_active/skipped、eye_resting、restricted_now、unrestricted_today和today_minutes。targets另提供页签、对应页面卡片、周编辑、时长字段、备份字段、电视页及规则总开关的生产布局中心；时长键盘使用其专属生产几何。观察不接受动作指令，不赋予认证。formatter空间不足不发布截断JSON，Host测试覆盖类型、布局中心、敏感文本排除与失败路径。

支持专项增加`disable_flag_present`、`recovery_active`两个布尔值和`diagnostic_status`整数（0空闲、1导出中、2成功、3失败，来自`PtcUiDiagnosticStatus`）。支持页的`card_*`中心来自`ptc_ui_support_card_rect`，不能沿用设置页坐标；不导出诊断路径或自由文本。

采样能力位于外壳：`input_sampling_version:2`、正整数`input_sequence`、布尔`input_active`。独立HID流另有整数`schema_version:1`、`input_stream_version:1`、与`input_sequence`相同的`sequence`、十六进制`instance`和十进制字符串`tick`。序号饱和而不回绕；新实例由原instance区别。UI帧中的采样字段只作为完成帧关联，down/up握手读取独立HID流。旧`input_sampling_version:1`的历史报告继续按原UI流核验，不能用于新的运行。采样状态只来自正常HID读取，观察路径不处理命令。

## 证据与离线复核

输出冻结 NRO/manifest、当批执行器／桥接脚本及哈希、完整工具回应（图片保留原 PNG）、UI 快照、业务回执、必要规则/state/nonce、仅含计数的认证摘要、自建 PID、原生输出及同 PID Windows XML。截图在 gallery.html 集中查看；UI快照不替代可读性与实际画面核对。

summary.json 将 pass、failed_or_unknown、not_executed 分开。evidence-manifest.json 绑定所有附件。先落盘再连续核验两次；离线 verify 只说明证据完整，不把运行失败变成通过。封存后另建补充材料，不改旧 raw 或报告。

采用采样驱动的通过报告还须离线核对`actions`：每个动作恰好一对相同输入参数的down/up，并有同实例活跃采样及序号更大的中立采样。`input-samples/`保存采样原件，checkpoint的`screenshot_call`指向对应calls/PNG；不能只写一个“已释放”布尔值签署成功。旧无采样批次继续按原schema范围验证，不追补其输入证据。
