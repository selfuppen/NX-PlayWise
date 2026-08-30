# devkitPro 开发容器

本目录提供维护者使用的 Windows + Docker Desktop + OpenSSH 默认环境。它是 `tools/package_remote.py` 的本机连接 profile，不是项目唯一可用的宿主系统。完整的通用要求、覆盖参数和安全边界见[开发环境指南](../docs/开发环境指南.md)。

## 准备挂载

先编辑 `docker-compose.yml`，将示例宿主卷路径改成包含本仓库的实际父目录。容器内默认把仓库映射到 `/ws/playwise`，必须与 `package_remote.py --container-path` 一致。

## 构建与启动

在本目录执行：

```text
docker build -t devkitpro:v1 .
docker compose up -d --force-recreate
```

Compose 默认把宿主端口 `1888` 映射到容器端口 `58791`，脚本以 `root@127.0.0.1:1888` 连接；映射已固定为 `127.0.0.1:1888:58791`。`PLAYWISE_BUILD_IMAGE` 同时写入构建 manifest；修改 Compose 或重新构建镜像后必须 `--force-recreate`，避免候选继续记录 `unknown`。当前 Dockerfile 启用开发用 root 密码，只适用于受信任的回环环境，不得把 SSH 端口直接暴露到局域网或互联网。自动化连接优先使用已授权私钥。

启动后从仓库根目录运行：

```text
python tools/package_remote.py
```

该脚本负责统一的清理、测试、构建、打包和产物校验；容器直接写入共享工作区的 `build/`。
