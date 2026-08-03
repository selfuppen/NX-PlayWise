# FROM devkitpro/devkita64:20260215
# 上面👆是dockerhub的官方镜像
FROM docker.jiaxin.site/devkitpro/devkita64:20260215

# =============================
# 1. 配置APT源 (Debian 12 - 阿里源)
# =============================
RUN rm -f /etc/apt/sources.list /etc/apt/sources.list.d/*.list 2>/dev/null || true && \
    echo "deb https://mirrors.aliyun.com/debian/ bookworm main non-free non-free-firmware contrib" > /etc/apt/sources.list && \
    echo "deb https://mirrors.aliyun.com/debian/ bookworm-updates main non-free non-free-firmware contrib" >> /etc/apt/sources.list && \
    echo "deb https://mirrors.aliyun.com/debian/ bookworm-backports main non-free non-free-firmware contrib" >> /etc/apt/sources.list && \
    echo "deb https://mirrors.aliyun.com/debian-security/ bookworm-security main non-free non-free-firmware contrib" >> /etc/apt/sources.list

# 设置时区为东八区（非交互式安装，避免弹出配置界面）
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        curl \
        git \
        openssh-server \
        tzdata \
        unzip \
        wget \
        zip && \
    ln -sf /usr/share/zoneinfo/Asia/Shanghai /etc/localtime && \
    echo "Asia/Shanghai" > /etc/timezone && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# 可选：设置环境变量（对某些应用有效）
ENV TZ=Asia/Shanghai

# =============================
# 2. SSH 配置
# =============================
RUN mkdir -p /var/run/sshd && \
    echo 'root:root' | chpasswd && \
    sed -ri 's/^#?PermitRootLogin .*/PermitRootLogin yes/' /etc/ssh/sshd_config && \
    sed -i 's@session\s*required\s*pam_loginuid.so@session optional pam_loginuid.so@g' /etc/pam.d/sshd && \
    sed -ri 's/^#?Port .*/Port 58791/' /etc/ssh/sshd_config

# =============================
# 3. SSH 服务启动
# =============================
EXPOSE 58791
CMD ["/usr/sbin/sshd", "-D"]
