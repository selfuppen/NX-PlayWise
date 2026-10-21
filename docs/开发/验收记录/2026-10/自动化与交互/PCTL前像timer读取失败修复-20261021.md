# PCTL前像timer读取失败修复

2026-10-21，Asia/Shanghai。接续NRO timer=0窗口恢复契约时，在生产 `switch_snapshot_settings` 发现实际缺口：145601成功后忽略1453 Result，查询失败仍发布完整size与默认timer=false。这个缺陷不能用于解释此前成功status明确返回的timer=0，但可能使后续恢复采用错误前像，故先修复独立软件路径。

新实现进入函数即将输出置为size=0；只有会话、完整0x44和timer读取全部成功才发布有效快照。1453失败返回既有PCTL读取错误并保留原IPC Result，会话所有出口正常关闭；不新增命令、权限或错误码，不调用timer启停。请求层沿用pctl_backup_failed，在写目标前拒绝。

适配器回归覆盖真实timer=true/false、1453和145601错误、四阶段会话错误及句柄回收。请求层分别注入指定日期额度/今日不限时的前像失败，检查backup错误、零目标/timer/恢复写入、rules/state原字节不变和无新活动恢复事务。统一入口红测在旧1453被忽略处稳定失败；修复后同入口clean的C/Python/UI/安全测试、三个生产组件、标准Zip和默认Eden打包校验全部通过，绿测约275秒。

执行：`python -u -B -X utf8 build/snapshot_timer_build_20261021.py red`、`green`、`close`，两次实际构建均调用 `python tools/package_remote.py --clean --port 18883 --docker-container playwise-snapshot-timer-20261021-01 --container-path /ws/playwise --build-image devkitpro:v1 --build-image-digest sha256:bf033de61ae6085217f7b17816e042ea2535f1182c6345b8291ca4c19c8d7ed9`，未跳过测试或关闭Eden。独立容器已移除，未使用邻接checkout或其他容器。软件包如实为dirty/pending，未部署，真机仍是原4e2。

附件 `device-reports/snapshot-timer-read-fix-20261021-01`，22件封存索引 `653787241a31e8d00611c55e946378c51333302f2f5eec512b2db62cfa841658`。原/新源码、失败与成功日志、标准包、Eden及manifest保存；`python -B -X utf8 device-reports/snapshot-timer-read-fix-20261021-01/verify.py` 从JSON读回、重验Zip/Eden，封存后两次通过。未写PCTL或设备规则，没有新图。下一独立批在提交后隔离checkout构建clean完整包组并保存所有源码原字节，不因PIN/图审条件停止该离线工作；新版本真机资格与NRO输入根因仍待实测。
