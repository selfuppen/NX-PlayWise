# 在线更新的应用级补充 CA

固定证书为 Certum Trusted Network CA（自签根证书），仅在 Gitee 来源的 libnx TLS context 中导入，随连接 context 释放，不修改系统证书库。GitHub 来源继续使用原系统信任库。连接仍验证对端、主机名和有效期；导入失败中止该来源，保持既有备用来源策略。

- 原始 DER：https://repository.certum.pl/ctnca.cer
- 发行方公布的指纹：https://www.certum.eu/en/cert_expertise_root_certificates/
- DER SHA-256：`5c58468d55f58e497e743982d2b50010b6d165374acf83a7d4a32db768c4408e`
- 查询日期：2026-11-04；有效期至 2029-12-31 12:07:37 UTC。
- 固定字节位于 `update_ca.h`，无运行时 CA 下载、可写路径或用户 CA 配置。

2026-11-03 宿主机从 Gitee 与 foruda 采到的链是 TrustAsia DV TLS RSA CA 2024 → TrustAsia TLS RSA Root CA（由 Certum Trusted Network CA 交叉签发）。这只支持补充信任链的实现选择，不能替代真机验证或证明首轮失败的完整原因。证书是公开验证材料，不包含私钥；分发不表示取得 Certum 的签发能力。

SDK 为 switch-curl 7.69.1-5，native libnx patch 的 SHA-256 为 `723c7d884fc7c39ae1a3115ba245bb8c1415da47bbd60ab8f943ca98f92ebc9a`；固定来源为 devkitPro/pacman-packages commit `9937f6ac3171c5dba7324de8edb4e6cc3af3c01f` 的 `switch/curl/switch-curl.patch`。其中 SSL_CTX callback 接收 `SslContext *`，系统 CertStore 自动生效；callback 不更改连接验证选项。

维护时从发行方重新核对指纹、有效期、撤销与链变化；不要仅按名称替换字节，也不要从失败对端获取证书并自动信任。根到期或来源链更换时，应通过真实源码变更、门禁和候选验证更新。
