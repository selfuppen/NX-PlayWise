# Gitee TLS修复beta.2候选与审核材料

2026-11-04，Asia/Shanghai。接续[Gitee原生TLS修复实证](Gitee原生TLS修复实证-20261104.md)。新的真实3.1.3-beta.2候选已完成软件及Eden发版前门禁、冻结和审核材料封存；尚未授权发布，未创建tag／Release，未部署本冻结候选到Switch。真实在线安装两阶段仍未执行，完整资格false。

## 冻结身份与两个拟公开附件

冻结源码 0298dbeaf65bc4f42874c35c9b0442567521c61d，release playwise-3.1.3-beta.2+0298dbeaf65b.a4777004589501c2，source_dirty=false、qualification=pending。网络证据schema2和应用级固定CA均为实际源码变更；不替换已公开beta.1的81d源码／附件，不制造虚假release身份。包含原生CA修复的内部6caf只是已验证Gitee元信息的程序基线，本冻结beta.2的设备成绩独立。

| 附件 | 字节 | SHA-256 |
| --- | ---: | --- |
| playwise-3.1.3-beta.2.zip | 3812857 | 2a6f44e70834507650933b9c81756e64acfcb571954cc2ee0cfbc5fcdf1c9b10 |
| playwise-complete-3.1.3-beta.2.zip | 3822269 | c792fc74f97069ef308d8dcd7c9665809eced51899605b7bb1a1eca94247ce13 |

内部Device Lab和Eden不拟公开。Eden NRO为3491534字节，SHA-256 0b2ca5b979398e6673b29858a850113bc8feca79bc0755f2e5a42ad68dd232da。精确附件、源码、模板、构建日志、manifest、原始Eden证据及独立verifier在 device-reports/gitee-beta2-candidate-20261104-01/。六项defaults与真实6caf基线逐字一致，为受支持的程序基线恢复提供兼容前提，不能据此直接宣称降级或新候选真机验收通过。

## 已执行软件门禁

完整冷构建的Host C/Python/UI、NRO6672与Overlay198共6870组渲染／PNG转换及交付包校验通过。冷构建时版本／文档改动尚未提交，dirty程序附件原件存入 gitee-beta2-preparation-20261104-01/cold-rejected/，明确不作候选或公开对象。提交0298后经统一入口再次运行完整基础门禁，生成clean标准／完整／Lab包和默认Eden NRO，均通过。[冻结源码CI41753835222](https://github.com/selfuppen/NX-PlayWise/actions/runs/41753835222)成功。

6870个原始PPM、6870个PNG和两个renderer元数据封存于 D:/PlayWise-Eden/evidence/gitee-beta2-previews-20261104-01/，不可变ZIP为1487028369字节、SHA-256 a78cba241a16d4adb963db628caaa48dec1afad161eb94bc6ef128d617750d00。归档逐成员原始SHA深度回读一次，其后完整ZIP哈希、JSON／索引重复核验通过。两个缓存的renderer、固定字体和wrapper哈希与最终Host字节匹配。两张实际中／英文Host更新页已由assistant独立复核可读；预览带sample data标识，不冒充真实设备offer。

新只读 restored_today_rules 核验函数支持已证明正常disable→restore事务留下的不生效元数据，拒绝原来存在今日调整、活动标记、错误日期、其他规则、缺失／额外字段或类型变化；成功／失败、JSON读回和本批真实前后像回归通过。它不提交控制请求，恢复事务／完整PCTL／timer／正常用量仍分别证明。

## 新Eden普通回归及清理

正式shared模式，来源 F:/Eden-MCP-Chinese/eden.exe，SHA-256 e81f589674c9db34d4c335427e70d3d5b93ed74bef0debf8079a2975d02c6986；每批独立profile，时间区index34，使用D盘共享固件缓存和F盘同卷程序硬链接。执行前登记容量、来源及本批拥有路径；未复制完整来源安装／profile或密钥固件原件。

- main-01：17/17通过，三次所属PID健康退出；run 2ae6a33a4f644f48a69f1a5c862dfcbe。
- support-01：6/6通过，两次所属PID健康退出；run b2e9e36eb23e49658d48a696f30412de。

trace均关闭；同ID/type业务回执、guest中立释放、业务／偏好持久化和实际重开通过。两套含各自必要前置，不能相加为23项互不重叠的功能。两批实际首次设置中文图均由assistant检查文字可读，独立绑定图哈希；没有用户全UI或真实游戏效果签署。

正式执行器先健康退出、封存及读回后自动清理本批D盘运行目录和F盘入口，两个cleanup/result.json均为removed；来源exe／profile和共享固件保留。脚本各重复离线verify两次，候选verifier再从写入JSON重推导并重复校验。未新增稳定性多批探测，beta.1原两次partial和未知根因未改写；其原失败现场保留及既定复查期限仍独立。

## 真机和恢复的适用边界

已有HTTP200／4400字节合法Gitee元信息的真机证据绑定内部clean6caf、PID156／boot fb74a1b0a6cfe1b2；不是本冻结beta.2的新成绩。GitHub备用未触发，下载域名和在线安装两阶段本批未执行。只读内部加载的ready_for_tests不作为在线安装器通过。

此前正常家长认证窗口已结束：恢复原请求成功，完整0x44和timer=1匹配，正常用量保留；原守护因不生效日期2498→2499的严格字典比较退出1，原失败保持。独立原UUID／accepted／receipt／raw及13文件／偏好／健康补充核验通过，没有重发恢复或写回旧state。规则有效语义恢复，字节不完全相同；程序仍运行实际6caf内部目标，未声称恢复原70d程序。213项实证及补充在 gitee-native-ca-result-20261104-01/ 与所属audit目录。本记录不是现在的设备状态保证，下一轮须重新前检。

## 审核对象和下一步

审核正文 review.md、拟公开说明 publish-notes.md、精确参数 proposed-publication.json、candidate-verification.json 已齐备；拟为GitHub预发行版、latest=false并同步Gitee。review-seal.json绑定4788文件与9项外部证据，SHA-256 9fc930908ba6d0e7c0e75bc25be539a14d8bd0303a2f2b52da56e1ec9ddf510c，JSON读回和两次重复封存核验通过；封存之后的新授权／发布／真机材料另建目录，不改原raw或审核包。

用户原要求“审核后再决定发布”，此前授权只覆盖81d对应的beta.1两个附件。此新beta.2等待独立发布决定。获授权后，第一阶段用真实下载作准备，执行请求49独立配置备份→合法包安装→重开→后台安全交接；通过并及时恢复后，经受支持流程恢复真实6caf程序基线，再做第二阶段自动／手动检查、通道、下载和整链路。备用网络失败实际前提单独证明，未知请求不重发，危险窗口独立及时恢复。没有新增本地ZIP安装入口、伪造offer、关闭TLS验证、强杀后台或默认整机重启。

实际主要命令：

```text
python -u -X utf8 tools/package_remote.py --port 31888 --release --clean
python -u -X utf8 tools/package_remote.py --port 31888 --with-complete --with-device-lab
python -u -B -X utf8 tests/devkit/test_auto_update.py
pwsh -NoProfile -File build/run-beta2-eden-20261104.ps1 -Suite main
pwsh -NoProfile -File build/run-beta2-eden-20261104.ps1 -Suite support
python -u -B -X utf8 device-reports/gitee-beta2-candidate-20261104-01/verify-candidate.py
python -u -B -X utf8 device-reports/gitee-beta2-candidate-20261104-01/seal-review.py --verify
```
