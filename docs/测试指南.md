# 测试指南

测试是架构的一部分。项目必须先在 host 环境证明行为，再使用真实 Switch 服务。

## 测试层次

单元测试：

- 面向纯 `common` 函数。
- 不使用文件系统。
- 不使用 libnx。
- 不依赖真实时间。

集成测试：

- 使用 `mem_storage`。
- 使用 `pctl_stub`。
- 使用 fake time。
- 端到端覆盖 request queue 和 control policy。

模拟器测试：

- 只验证 companion UI 和文件协议。
- 不声称验证真实 PCTL 行为。

真机测试：

- 验证 boot2、PCTL 读写、raw block、suspend 和 play timer 行为。

## 当前测试入口

本地 Python 回归：

```powershell
python .\tests\mvp\test_token_v1.py
python .\tests\observe\test_observe_queue.py
```

远程或具备 C 编译器的 host：

```sh
make
make test-host
make test-python
```

`make` 当前会编译并运行 `tests/c/test_host_core.c`，然后运行两个 Python 回归测试。

## 当前 C Host 覆盖

`tests/c/test_host_core.c` 当前覆盖：

- C token v1 与 Python fixture 一致。
- HMAC 校验、错密钥、错日期、重复 nonce、超上限。
- day index、weekday 和跨天 bedtime。
- 错误 reason 映射。
- observe dry-run 策略。
- grant 写入策略、backup gate 和 nonce 消费。
- raw block capability gate。
- `disable.flag` 优先级。
- status request pending -> processing -> done -> result。
- grant request 调用 `pctl_stub` 并写 backup/ledger。
- backup failure 阻止 PCTL 写入且不消费 nonce。
- stuck processing request 恢复。

## 仍需补充的覆盖

- 更完整的 request/result schema validation。
- `set_today_limit`、`add_today_minutes`、`disable_today_limit`。
- `restore_today_policy`、`set_weekly_template`、`set_bedtime`。
- `parent_unlock_start`、`parent_unlock_end` 和过期。
- `probe_raw_block`、`probe_suspend` 在 stub 中更新 capability。
- PCTL read/write failure 的更多 result 断言。
- storage failure 的可恢复路径。

## Fixture 要求

Fixture 应生成，不手写。

每个 fixture 应定义：

- case name。
- device id。
- grant secret。
- date 或 day index。
- nonce。
- minutes。
- expected token。
- expected result。

`tools/make_fixtures.py` 必须从与 `tools/grant_code.py` 相同的协议规则生成 deterministic fixture。

## 验收门槛

启用真实 PCTL 写入前：

- 所有 host 单元测试通过。
- 所有 host 集成测试通过。
- `observe` 验证有效码但不消费 nonce。
- 坏码路径从不消费 nonce。
- backup failure 阻止写入。
- capability gates 在 probe 前拒绝 raw block 和 suspend。

启用 `enforce` 前：

- `grant` 阶段已经通过真机最小写入测试。
- 拒绝用例在真实硬件通过。
- backup 文件可读且有恢复价值。
- event log 能记录成功和失败路径。

## 远程容器验证

共享 devkitPro 容器通过本地 SSH 别名访问：

```text
249-nintendo-switch-dev
```

远程仓库路径：

```text
/ws/switch-play-time-control-local
```

本地和远程都使用 `master` 分支。远程不会自动同步本地工作区；必须先本地提交并推送，再让远程 `git pull --ff-only origin master`。

当前远程完整验证命令：

```sh
ssh 249-nintendo-switch-dev 'cd /ws/switch-play-time-control-local && git pull --ff-only origin master && make'
```

需要显式设置 devkitPro 环境变量时使用：

```sh
ssh 249-nintendo-switch-dev 'export DEVKITPRO=/opt/devkitpro; export DEVKITARM=/opt/devkitpro/devkitARM; export DEVKITA64=/opt/devkitpro/devkitA64; export PATH=$DEVKITA64/bin:$PATH; cd /ws/switch-play-time-control-local && git pull --ff-only origin master && make'
```

Package 验证示例：

```sh
ssh 249-nintendo-switch-dev 'cd /ws/switch-play-time-control-local && git pull --ff-only origin master && make package-safe package-observe'
```

默认 package 不应包含 `boot2.flag`。只有显式运行：

```sh
python3 tools/package_sdmc.py --mode observe --boot2 --out build/packages/observe-boot2
```

才应生成 boot2 flag。
