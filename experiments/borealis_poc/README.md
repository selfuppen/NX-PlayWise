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

## 模拟器兼容性

`build/eden-test/pctc-eden.nro` 使用 libnx 软件 framebuffer；它能在 Eden 运行，
不代表 Eden 同时支持 Borealis 使用的 guest GPU/deko3d 路径。当前维护环境的 Eden
v0.2.1、Intel Iris Xe 32.0.101.7085 会在 deko3d/NVDRV 初始化阶段退出，因而不能用
这组环境完成 PoC 视觉验收。Ryujinx 1.3.3 还会遇到 devkitA64 GCC 15.2 异常展开器
中的 GCS 指令兼容问题。

PoC 已把无关的无线优先级调用从该离线目标中排除，并把单独 `S8` stencil 改为
兼容性更好的 `Z24S8`，但这不能补足模拟器缺失的 guest GPU 能力。出现上述日志特征
时应改用真机，或使用已证明能运行 deko3d homebrew 的模拟器/GPU 组合；不得把白屏、
闪退或仅仅构建成功记作视觉通过。
