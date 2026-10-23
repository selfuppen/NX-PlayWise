# Eden 自动化测试（发版前）

此入口在隔离 profile 中执行真实 NRO 输入，配合进程内模拟后台、回执和持久化断言。它不证明真实 PCTL、IPC/SD 队列、Tesla、休眠、底座或生产候选资格。

## 执行时机

日常基础测试不运行此入口，也不自动启动模拟器或重复稳定性批次；日常使用 Host 确定性回归和必要的统一构建／产物校验。Host 中对执行器及模拟逻辑的单元测试继续保留，默认 Eden NRO 构建／校验也保持不变，两者均不启动模拟器。

Eden 场景回归集中到发版前。固定候选 NRO／manifest 后，默认执行一批下面的最小回归，并补充受本次变更影响的页面、规则或恢复场景。最小回归已包含一次关闭重开，不把能力调查时的多批稳定性复测设为每次发版的固定要求；只有明确的稳定性验证目标才另排有界批次。原始证据照常封存，离线核验重复执行，避免把“重复核验报告”误解为“重复启动整套模拟器”。

## 准备与运行

先经统一构建入口生成当前源码的 Eden NRO。C、NRO 或 package 改动的最终门禁仍为 `python tools/package_remote.py`，保留默认 Eden；`--only eden` 只用于可选迭代。

需要带 protocol 2 的定制 Eden、配置好的 profile，以及装有 `eden-mcp 0.5.0` 的外部 Python。执行器本身和 Host 回归只用标准库；不会联网安装依赖、修改全局 MCP 配置或原 profile。外部依赖可显式用 `--mcp-site` 指定。

```powershell
python -X utf8 tools/automation/eden_runner.py run --eden "<Eden目录>/eden.exe" --profile "<Eden目录>/user" --mcp-python "<MCP环境>/python.exe" --mcp-site "<MCP依赖目录>" --out "device-reports/eden-automation/<全新批次>"
python -X utf8 tools/automation/eden_runner.py verify --run "device-reports/eden-automation/<批次>"
```

输出目录必须不存在；每次创建新的 `build/eden-runtimes/<随机ID>`。只复制 config、keys、nand，不复制操作者 SD/认证数据；全部存储路径重写到新 profile。敏感运行材料保留在该私有目录，不进入证据清单。固定测试规则为每周每日 120 分钟，PIN 由正常首次设置创建默认 `110`；不接受或记录家庭 PIN。

可用 `--runtime-root <本地可写目录>` 将私有 exe／profile 放在指定磁盘；报告仍由 `--out` 单独指定。此入口始终复制所选软件，不能把它描述成直接操作原安装。开始输入前同时核对 MCP 的 effective_profile_root 与实际 Eden 日志中的 ConfigDir／SDMCDir；不同则停止。本机二进制的直启会优先使用相邻user目录，不能依赖 MCP 的 EDEN_USER_DIR 实现隔离。

语言／地区／渲染器默认完整沿用来源profile，NRO保留“跟随系统”，不强制切到英文。`--time-zone-index 34` 可只在私有配置中显式固定本build的PRC时区；fixtures/profile-settings.json 记录来源值、实际值和覆盖项，profile-settings-final.json 记录退出后持久化值。启动及重开均留截图，验证中文起点时须结合日志和画面，不只看配置数值。

默认沿用 profile 的渲染器。可用 `--renderer-backend 0` 在当批私有配置中选择 OpenGL，`1` 为 Vulkan；同时取消该配置的默认值标记，原 profile 不变。fixtures/renderer.json 记录请求值、实际配置值及配置哈希，eden-log.txt 留存实际渲染器日志。只有指定后端的本批结论有效，不能据一次通过声明所有 GPU／后端稳定。2026-10-23 的本机对照见[首次执行记录](验收记录/2026-10/自动化与交互/Eden最小自动回归落地与首轮执行-20261023.md)。

所选定制build的`backend=3`日志为`OpenGL_GLASM`，与上一轮`0`不同；要沿用该模式时省略覆盖参数，不凭“都是OpenGL”改写枚举。中文可写副本复测见[中文回归与稳定性记录](验收记录/2026-10/自动化与交互/Eden中文可写副本最小回归与稳定性复测-20261023.md)。

默认总期限 300 秒、最多 180 次输入，无自动重试。首次设置 → 重开应用 → PIN 取消/错误/正确 → 30分钟码成功/重复拒绝依次执行。每次输入先验证运行状态与新鲜页面快照；失败阻止依赖输入。业务回执必须为新出现的同 ID/type 文件，超时保持未知、不换 ID 重发。重复码测试是有意的新预览请求，与网络重试分开。

只有全部场景、两次进程退出和 Windows 事件审计均通过，整体才为 pass。零退出码及 MCP isError=false 不覆盖失败启动或同 PID 崩溃。事件日志不可读为未知，不算通过。仅停止本次服务器创建的进程，不接管已有 Eden。

## Eden 只读 UI 快照 v1

触摸脉冲为400毫秒、按钮脉冲为200毫秒，工具返回后等待1.2秒，包含释放后的中立间隔；Eden 工具返回只代表已安排输入。每个数字仍须由后续长度断言确认，不自动补发。Windows 原子替换快照时的只读占用可在1秒内重读，持久占用仍停止。

Windows快照读取以FILE_SHARE_READ／WRITE／DELETE打开短时句柄，从同一读句柄取得内容及修改时间，减少读侧阻碍模拟SD删除／替换的机会。Host回归验证旧名字可在读句柄打开时删除、旧内容仍可读、句柄关闭后新文件可发布；不把Windows替换语义或模拟SD当作真实Switch SD原子性的证明。

模拟 PCTL 的加时读路径在实际读取时推进累计时间并采样日期／时间戳，避免把页面轮询缓存冒充新鲜加时读数；生产核心的新鲜度门禁保持不变。它仍然只是模拟累计使用量。

仅 `PLAYWISE_EDEN` 编译；由测试 profile 的 `support/automation-observe.flag` 显式启用。绘制路径最多每秒10次写入 `support/automation-ui.json`，不提交请求、不读取动作指令、不改认证或业务状态。快照失败不影响产品流程，执行器因缺少/过期快照停止输入。

外壳：`schema_version:1`、内嵌 `release_id`、本次 NRO `instance`（十六进制 tick 起点）、正整数 `sequence`、十进制字符串 `tick`、`ui` 对象。instance 仅用于测试进程观察，不是主机 boot 身份。

ui 白名单：view、parent_page、overlay、setup_step、setup_active、waiting、status_loaded、error_code、selected_index、pin_length、numpad_length、numpad_purpose、code_result_failed、code_result_pending、overlay_animating，以及 targets。枚举沿 `ui_model.h`：view 0孩子/1家长/3向导，overlay 0无/3确认/4数字输入/12兑换结果/13认证错误/22 PIN。targets 的 width/height 为1280/720，坐标中心直接来自生产布局函数；提供 setup_primary、child_code、pin_confirm、pin_0..9、code_0..9。执行器不写死页面 X 偏移，并等待弹层入场结束。

不导出 PIN、加时码、配对密钥、弹窗自由文本或可复用认证材料。长度只用于等待每次独立输入处理完成；不能作为认证成功证明，仍核对进入页面、认证计数及对应业务回执。

## 证据与离线复核

输出冻结 NRO/manifest、当批执行器／桥接脚本及哈希、完整工具回应（图片保留原 PNG）、UI 快照、业务回执、必要规则/state/nonce、仅含计数的认证摘要、自建 PID、原生输出及同 PID Windows XML。截图在 gallery.html 集中查看；UI快照不替代可读性与实际画面核对。

summary.json 将 pass、failed_or_unknown、not_executed 分开。evidence-manifest.json 绑定所有附件。先落盘再连续核验两次；离线 verify 只说明证据完整，不把运行失败变成通过。封存后另建补充材料，不改旧 raw 或报告。
