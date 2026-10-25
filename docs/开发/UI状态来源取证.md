# UI status 来源的限期取证

标准候选manifest声明`ui_status_trace_version:1`时，NRO和Overlay提供默认关闭的status来源日志。它用于区分同一观察窗内的多个客户端、NRO直接Y刷新及恢复/焦点事件自动刷新。日志不修改HID权限、不新增Switch服务、不注入按键、不提交额外status或控制请求；也不证明游戏实际可用或输入隔离。Eden不启用该生产取证。

先完成统一构建与默认Eden校验，冻结实际clean标准Zip及三个组件，并核对安装文件和运行身份。配置仅在各前端进程初始化时读取；不用重启后台或整机。NRO需正常重开，Overlay需正常退出旧模块再重新加载，不把隐藏后再次显示当成重载。更新/后台交接期间不启用此取证；先完成交接，再绑定实际目标boot，按有界计划准备日志和收尾材料。

配置位于`sdmc:/switch/playwise/support/ui-status-trace.json`，具有且仅具有七个唯一键：

```json
{"schema_version":1,"run_id":"0123456789abcdef0123456789abcdef","commit":"<实际40位commit>","release_id":"<实际release ID>","backend_boot_id":"<实际16位boot>","expires_mono_ns":"<规范十进制纳秒>","max_records":32}
```

`expires_mono_ns`使用与标准raw相同的设备单调时钟，不使用Overlay可能尚未初始化的Unix时间。初始化时必须在未来且距现在不超过900秒；`max_records`为每个客户端实例1—128条。run/commit/boot格式、候选及实际`handover/runtime-ready.json`的release/boot/profile均须一致，拒绝重复键、额外键、非法类型及过期配置。跨整机启动不得复用配置，后台boot变化也停止旧实例取证。

仅离线生成配置，不上传或激活：

```text
python tools/device/ui_status_trace.py prepare --manifest <冻结build.json> --runtime <实际runtime-ready.json> --sample <新鲜有效标准sample.json> --run-id <全新32位hex> --duration-s 600 --max-records 32 --out <独立目录/ui-status-trace.json>
```

调用方另行核对sample的新鲜度、原raw/候选哈希和同一OS单调时间基准，原字节写入并回读专用配置后才正常重开前端。该命令不建立执行租期、重开应用或改变设备。不能只因配置生成成功认定任何前端已加载它。

前端输出`support/ui-status-trace-<run>-<client>-<实例单调时间hex>.jsonl`，同名已存在则拒绝启用，绝不覆盖。头记录绑定实际编译commit/release、后台boot、client（nro/overlay）、开始/截止tick和条数上限。每次原status提交记录sequence、原请求ID、`origin`、设备单调纳秒、`focus_state`及`transport_status`。它是前端提交结果，不是后台ok回执，必须另配原ID/type/content及结果。

`origin`允许`other/button_y/resume/focus/resume_and_focus/overlay_ready/touch/countdown_end`。NRO孩子页和首次设置页的直接Y有明确标签，未标记的其它入口保持other；恢复与焦点事件可合并，等待其它请求期间不丢原因。Overlay只在处理当前输入回调时使用Y/touch原因，回调返回后清除，握手后的自动status为overlay_ready，避免把先前输入归给后续自动刷新。`countdown_end`只在孩子眼前的护眼休息或就寝倒计时归零时使用，每个break/window实例最多一次，且必须先在同一浮窗内观察到该实例倒计时大于零；尚未确认新建档或服务重启时不得据此推断限制已解除。NRO focus为已有libnx缓存状态；Overlay固定-1，不引入AM或HID查询来假造焦点证明。日志不保存完整按键、请求payload、PIN、密钥或离线码。

达到记录上限、绝对期限、配置删除/原字节变更、后台身份变更或非法时钟时锁存停止；正常客户端退出也保存stop。每次提交前重读配置和runtime，运行中改配置不能延长旧实例。启用时存在有限SD读取与追加开销。任一追加失败立即停用且不重放该行，不改变原请求结果或触发额外请求；失败日志可能没有完整stop，必须保留原字节并判为不完整。空闲前端不会为了检测期限而主动提交或轮询；配置移除后下一次原status或正常退出才产生终末记录。

收尾优先按既有业务恢复边界完成，再仅归档本批配置。正常退出已加载日志的客户端，核对各stop及两次间隔拉回相同。不能为了补终末日志强杀Switch后台、反复提交业务或延迟恢复。trace与PCTL标准raw分别有自己的停止条件，任何一条停止都不自动证明另一条停止。

将完整停采日志与有界观察器保存的`accepted-*.json/result-*.json`离线配对：

```text
python tools/device/ui_status_trace.py audit --manifest <冻结build.json> --runtime <本批runtime-ready.json> --run-id <本批run> --trace <nro.jsonl> --trace <overlay.jsonl> --receipts <原回执目录> --out <新目录/provenance.json>
```

审计拒绝坏尾行、非完整/失败终末、重复JSON键、越界tick、错候选/boot/run、重复ID或多实例歧义、ok回执与失败提交冲突。没有来源记录的ID保持null，不按ID长短猜客户端。重复执行只核对输出相同，不覆写不同结果。字段`all_matching_ok_attributed`只表示原回执的客户端及调用原因已绑定，`qualified:false`、输入隔离和游戏效果均保持独立。原回执观察器的多来源提示不回写；新增来源结论作为另一个附件封存。
