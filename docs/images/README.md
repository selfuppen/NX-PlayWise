# 文档预览场景清单

[文档索引](../README.md) · [中文使用指南](../使用/使用指南.md#查看今日详情) · [English User Guide](../使用/USER_GUIDE.md#viewing-todays-rule-details)

使用指南按“查看余额与安排 → 调整额度 → 解释限制 → 浮窗兑换与恢复”展示功能。主机应用保留浅暗主题示例，浮窗使用固定深色；截图中的日期、分钟数、代码和密码掩码均为固定示例数据。所有自动图带 `HOST PREVIEW / SAMPLE DATA`，不代表真机计时、解除效果或输入验收。

## 生成与同步

完整同步清单位于 [`tools/build/sync_doc_previews.py`](../../tools/build/sync_doc_previews.py) 的 `PREVIEW_FILES`。主机应用来自 [`tests/ui_preview/render.c`](../../tests/ui_preview/render.c)，浮窗来自 [`tests/overlay_preview/render.cpp`](../../tests/overlay_preview/render.cpp)；两者调用当前生产绘制代码。

```text
python tools/package_remote.py --with-previews
```

此命令执行统一构建门禁、生成预览并同步中文 `usage/` 与英文 `usage-en/`，默认同时打包和校验 Eden 测试应用。只更新图片可用 `python tools/package_remote.py --previews`；它不构建安装包，也不运行完整测试。生成后可运行 `python tools/build/sync_doc_previews.py --check`，逐字节核对两种文档图片与本轮生成物一致。

路径映射：简体中文 `build/ui-previews/<场景>` → `docs/images/usage/<场景>`；英文 `build/ui-previews/en/<场景>` → `docs/images/usage-en/<场景>`。繁体中文与其余主题/边界场景仍由生成器输出到 `build/ui-previews/`，文档按用途选图，不全量复制。配对二维码页面由同一渲染器生成（`parent/pairing-qr-demo-light.png`），二维码编码官方网页应用地址（`https://selfuppen.github.io/NX-PlayWise/`），不使用真实家庭凭据；历史真机照片与网页示意仍按使用指南单独维护。

## 从 174 张复核预览选取的补充场景

选景参考“新UI复核与60分钟测试窗口”的 Host 预览复核：本机原始索引为 `device-reports/ui-opt-preview-review-after-20261016-01/gallery.html`，复核结果为同目录 `review.json`。该批包含 24 个主机场景的三语浅暗主题（144 张）与 10 个固定深色浮窗场景的三语图片（30 张）。这些本机归档不随文档分发，也不是构建依赖；原件保持不变。

已有同步清单覆盖孩子首页、自主缓冲、正常规则裁决、加时/不限时确认、浮窗输码与兑换确认等功能。本次补充以下 15 张选景，每张均同步简体中文和英文版本。场景标识沿用原复核索引，图片始终从当前代码重新生成。

| 场景（相对 `usage/` 或 `usage-en/`） | 展示的软件功能与典型问题 | 使用指南位置 |
| --- | --- | --- |
| `parent/today-planning-dark.png` | 深色时间轴，区分护眼休息、恢复与掌机限额耗尽 | 查看今日详情：安排今天怎么玩 |
| `parent/parent-details-eye-care-resting-dark.png` | 护眼休息中仍有总余额，显示本次休息与之后的推演 | 查看今日详情：安排今天怎么玩 |
| `parent/today-unlimited-tv-dark.png` | 今日不限时，护眼、就寝、电视规则仍独立生效 | 查看今日详情：安排今天怎么玩 |
| `parent/parent-details-rules-bedtime-active-dark.png` | 有余额但就寝生效，解释当前阻断来源 | 查看今日详情：为什么不能玩 |
| `parent/today-multiple-limits-light.png` | 总额度、就寝、护眼与非电视限制并发 | 查看今日详情：为什么不能玩 |
| `parent/today-stale-light.png` | 状态过期，余额和未来推演保持待确认 | 查看今日详情：为什么不能玩 |
| `parent/today-rules-stale-light.png` | 规则页同样显示待确认，避免未知状态被当作放行 | 查看今日详情：为什么不能玩 |
| `parent/today-rules-explanation-light.png` | 解释额度优先级、独立守护与加时边界 | 查看今日详情：规则来源与关闭状态 |
| `parent/today-policies-off-light.png` | 健康规则关闭、今日不限时时的明确状态 | 查看今日详情：规则来源与关闭状态 |
| `parent/today-rules-control-off-light.png` | 控制停用，区分配置规则与当前执行状态 | 查看今日详情：规则来源与关闭状态 |
| `overlay/overlay-code-danger.png` | 兑换后预计无可玩时间，展示警告与长按确认 | 游戏内浮窗：典型场景 |
| `overlay/overlay-parent-pin-error.png` | 家长密码错误与重新验证入口 | 游戏内浮窗：典型场景 |
| `overlay/overlay-parent-pin-cooldown.png` | 连续错误后的冷却及长内容滚动 | 游戏内浮窗：典型场景 |
| `overlay/overlay-console-details.png` | 本机累计、原捕获时间与独立就寝限制 | 游戏内浮窗：典型场景 |
| `overlay/overlay-console-details-unknown.png` | 累计暂不可用与刷新入口，缺失读数不作零 | 游戏内浮窗：典型场景 |

README 只展示孩子首页、深色时间轴、额度来源与多项限制四张概览。使用指南在对应操作下展开示例；排版压力场景（密集时间轴、超长指定日期等）保留在开发预览中，不混入日常操作示例。
