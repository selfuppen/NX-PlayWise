# 镜像构建命令：
 `sudo docker build -t devkitpro:v1 .`

 # 启动命令
 `docker-compose up -d`

# 远程宿主机 SSH 配置

 ```sshconfig
  Host renqi-nintendo-switch-dev
    HostName 5.78.109.249
    Port 22
    User ygq
    PreferredAuthentications password
    PubkeyAuthentication no
    PasswordAuthentication yes
```

连接时由 OpenSSH 提示输入宿主机密码。不要把密码写入仓库、SSH 配置或命令行参数。

# 在远程容器内执行命令

```powershell
ssh renqi-nintendo-switch-dev "docker ps --filter name=devkitpro-ssh-v1"
ssh renqi-nintendo-switch-dev "git -C /home/ygq/nintendo/switch-play-time-control-local status --short --branch"
ssh renqi-nintendo-switch-dev "docker exec devkitpro-ssh-v1 sh -lc 'cd /ws/switch-play-time-control-local && make test-host'"
```

宿主机仓库位于 `/home/ygq/nintendo/switch-play-time-control-local`，负责 Git 拉取；同一目录挂载到容器的 `/ws/switch-play-time-control-local`。远程编译必须通过 `docker exec devkitpro-ssh-v1 ...` 执行，不要在宿主机环境中直接运行项目的 `make`。
