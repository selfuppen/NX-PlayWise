# Overlay 内存与死机风险自查

调查日期：2026-10-13。用户反馈：打开或操作 PlayWise 浮窗时频繁死机。

结论：本地源码存在可解释浮窗崩溃的内存增长和线程竞态，已经修复；缺少发生故障时的 crash report、寄存器/调用栈和实际 loader 内存读数，不能认定这就是本次整机死机的根因。当前结论不依赖远端最新版或其他版本的行为。

## 检查范围与版本

- PlayWise 的 `companion/overlay/source/`、`bridge.*`，以及共享 transport、Switch IPC client、JSON 解析和 SD 存储。
- 项目实际编译的 `companion/overlay/vendor/libtesla/include/tesla.hpp` 和 `stb_truetype.h`。
- 固定上游：WerWolv/libtesla `f766e9b607a05e9756843cbd62b3bfb98be1646c`；本地字体与兼容补丁以 `UPSTREAM.txt` 登记为准。本次只新增本地补丁，没有升级上游。
- 审查包含当前工作区已有的 UI 修改。Host/Eden 不执行真实 Tesla/VI/HID 运行环境，不能替代 Switch 验证。

## 已确认的缺陷

| 缺陷 | 触发条件与影响 | 修复 |
| --- | --- | --- |
| 无上限字形缓存，位图由裸指针持有且未释放 | `drawString` 的函数内静态 map 按字符、字号、简繁字体偏好增长；切页面或状态文案变化可继续增加。同一字形重复绘制不会逐帧新增，但整个进程内没有淘汰上限；map 析构也不会释放裸指针指向的位图 | 固定 512 项元数据、256 KiB 位图预算，按桶近期使用淘汰；分配新位图前释放腾出预算，位图由 deleter 调用 `stbtt_FreeBitmap`；renderer 退出清空 |
| 测量文字也栅格化 | 透明色调用用于测量、居中、右对齐、逐次截短；原实现先建位图再检查 alpha，连最终不显示的文本也占用缓存 | 测量只读取字体 advance/bounds；可复用已缓存指标，但不新建位图 |
| 不需要的 SD emoji 字体占用 | 浮窗本身过滤 emoji，却仍查找并加载两个共享 Overlay 目录中的外部字体；最多额外保留 256 KiB，读取失败后的 vector clear 也保留 capacity | 标准 PlayWise 禁用该加载分支；renderer 退出释放 vector capacity |
| 输入线程直接修改显示/隐藏状态 | 主线程绘制/推进动画时，后台线程直接调用 `hide()`；`running`、`overlayOpen` 为未同步 bool，属于 C++ 数据竞争 | 生命周期标志改为 atomic，后台线程只设置隐藏请求，由主线程处理 GUI |
| 事件与线程的启动/退出顺序 | 原实现先启动线程再创建 combo event，退出先关闭事件再 join；线程可能访问未准备好或已关闭的事件 | 检查事件/线程创建结果，先创建事件和 GUI 再启动线程；join 后才关闭事件 |
| 在输入互斥锁内做 IPC/SD 操作 | `handleInput` 可能同步提交请求或验证 PIN，阻塞时同时阻止后台扫描 HOME/电源/组合键，表现为输入失去响应 | 锁内只复制输入快照，锁外执行 GUI 与请求逻辑；设置解析移出 4 KiB 栈的后台线程 |

256 KiB 只约束缓存位图。固定元数据、stb 栅格化临时分配、UI 对象、JSON、服务、图形缓冲及 loader 自身仍有占用；不能把它当作 overlay 总内存上限或完全免疫 OOM 的承诺。位图分配返回空时释放该条缓存预约，保留本帧文字 advance，下次绘制可以重试。stb 内部临时分配在极端内存不足时仍可能失败。

## 未发现累积泄漏的路径

- GUI 只创建一个 Frame 和一个 CustomDrawer，`Gui` 与 `OverlayFrame` 析构分别释放顶层元素及内容；页面切换是在同一 GUI 内切状态，没有不断 `new` 页面。
- `setInitialize/setExit` 配对；Bridge 退出先 cancel 等待事件，再退出 IPC service 与其保留的 SM 引用，最后卸载 SD。
- IPC 等待 token 在结果、错误回退、下一次提交/cancel 或退出时执行 `eventClose/free`。超时路径可能保留一个等待 token 到下次提交/退出，数量有界，不能据此声称每次刷新都泄漏。
- 检查到的配置、认证、结果和 transport JSON 解析分支均有 `cJSON_Delete`；SD 文本读写、诊断日志及目录遍历有 `fclose/closedir`。
- Renderer 的 framebuffer/window/layer/display/vsync event/VI 有退出清理；像素写入检查屏幕边界，家长列表 scissoring 的 push/pop 配对。
- 同一个 Overlay 的隐藏与真正退出不同：隐藏后保留 GUI、图形和服务资源供再次打开，当前字形缓存也保持有界；资源释放发生在退出 renderer/services 或进程结束时。

## 尚需真机证据的风险

标准 `pctc:u` 连接使用同步 `smGetService`，请求使用同步 `serviceDispatch`。界面里的请求超时从这些调用返回之后才继续推进，不能中断卡住的 SM/IPC、VI vsync 或 SD 驱动调用。因此后台缺席/无响应、loader/CFW 组合、其他 sysmodule 争用资源，仍可能导致浮窗停住。取消等待事件与释放内存不能解决这类同步调用阻塞。本次没有更改 IPC 提交/持久化语义。

没有实际 loader 版本和进程内存读数时，不把文档中的约 6 MB 资源配额或历史补丁所说的 4 MB heap 当作本机测量结果。两个中文字体来自系统共享字体，没有把整份 TTF 复制进私有 vector。

## 回归与复现

`tests/overlay_memory/test.cpp` 执行真实的缓存实现，以确定性合成位图覆盖淘汰前释放、碰撞桶近期使用顺序、字节预算、同 key 替换、超大位图拒绝、分配失败后重试、重复 clear、析构和 100,000 次字符/字号/语言键变化；记录 peak 和最终 live allocation。它证明缓存所有权和预算，不测量 Switch 整机 RSS。

`tests/devkit/test_switch_ipc_lifecycle.py` 检查有限缓存、位图释放、无额外 emoji 字体、atomic 标志、GUI 隐藏线程与事件/线程生命周期约束。统一构建入口运行这些 Python 回归和 host C/C++ 测试，交叉编译标准组件，并默认构建/校验 Eden 测试应用。

```text
python tools/test.py
python tools/package_remote.py
```

本次实际执行结果：

| 命令 | 结果 |
| --- | --- |
| `python tests/devkit/test_switch_ipc_lifecycle.py` | 通过 |
| `python tools/test.py` | 通过 |
| `python tools/package_remote.py` | 默认端口 1888 连接拒绝，未进行构建 |
| `python tools/package_remote.py --port 18881 --docker-container playwise-explain-build` | Host C/C++、Python、UI 原语、标准 Overlay 编译、标准包和 Eden 构建/校验全部通过 |
| 上一命令追加 `--with-device-lab` | 共享 libtesla 的 Device Lab Overlay 编译及实验包隔离校验也通过；标准包与 Eden 校验继续通过 |
| `git diff --check` | 通过 |

压力测试输出为 `100000 inserts, peak=262144 bytes, live=0, bitmaps=0`。最终候选为 `playwise-3.0.3-alpha.zip`，SHA-256 为 `455532cc63e7b4b36f36804c60bd8a4f741c308395d3a98b2674ec7e7ebd82a0`；qualification 仍为 `pending`。标准 Overlay NRO image 为 1,007,616 字节，BSS 为 61,440 字节；map 中固定 Renderer 为 42,392 字节。这些是编译产物尺寸，不是实际进程内存峰值或可用 heap 测量。

真机复验记录候选 Zip SHA-256、机型、HOS、Atmosphère、Ultrahand/Tesla loader 版本与叠加 sysmodule。优先只打开概览，再分别持续刷新、进出家长页、输入/取消、HOME/电源隐藏与返回管理器重新启动；记录故障发生在哪一步。保持游戏、系统与其余配置一致，对比旧/新候选。没有新的验证记录，不将候选标记为 qualified。

若仍死机，保留原始错误画面/程序名/Title ID、发生时间、Atmosphère crash report/fatal 文件及 `switch/playwise/logs`；对照 `client=overlay` 的 IPC 日志确认连接、提交之前还是之后失去响应。硬冻结可能没有报告，须保留上述 A/B 复现记录。此处仅列取证需求，没有根据未记录版本的 Atmosphère 源码推断路径或根因。
