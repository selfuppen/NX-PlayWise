# Borealis UI PoC

这个目标只用于比较 Borealis/NanoVG 与现有 Companion 自绘渲染层的边缘、字体、阴影、
手柄焦点和触摸体验。它不连接 `pctc:u`，不读取或写入 PlayWise 数据，不调用 PCTL，
也不进入标准、Complete 或 Device Lab 分发包。

统一构建入口：

```text
python tools/package_remote.py --only borealis-poc
```

也可在完整门禁中附带构建：

```text
python tools/package_remote.py --with-borealis-poc
```

验收时依次查看孩子首页、家长今日页和确认弹窗；用“切换浅色/暗色”检查两套色板，
用方向键/A 键和触摸分别操作三个导航按钮。PoC 通过 Eden/真机视觉验收之后，才能
逐页替换生产展示层；`PtcUiModel`、协议、队列与控制逻辑保持不变。
