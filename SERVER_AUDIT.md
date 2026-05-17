# SERVER_AUDIT.md

审计日期：2026-05-13  
当前整理日期：2026-05-18  
阶段：S0.1 只读审计，S0.4 已完成设备注册与心跳实机验证

本文是服务器环境的事实记录和避让边界。部署/运行命令已合并到 `esp-ai-terminal-server/README.md`，避免多份文档重复维护。

## 1. 审计方式

- Codex 未直接连接服务器。
- 用户手动 SSH 登录服务器并执行只读命令。
- S0.1 审计阶段未安装、删除、重启或修改任何服务。
- 后续 S0.4 临时公网调试由用户手动在腾讯云安全组放行端口，并前台运行 Uvicorn。

## 2. 基础环境

- 操作系统：Ubuntu 22.04 LTS (Jammy Jellyfish)
- 内核：Linux 5.15.0-106-generic x86_64
- 主机名：VM-0-16-ubuntu
- 当前用户：ubuntu
- 内网地址：10.0.0.16
- Docker bridge：172.17.0.1
- Carshow Docker 网络：172.20.0.0/16，网关 172.20.0.1
- CPU：2 vCPU，AMD EPYC 7K83
- 内存：约 3.3 GiB，总可用约 2.4 GiB
- Swap：0B
- 根分区：约 69G，总可用约 51G
- 文件句柄限制：1024

## 3. 现有项目

现有项目目录：

```text
/home/ubuntu/Carshow
```

现有 Docker Compose 项目：

```text
project=carshow
network=carshow_default
```

现有容器：

```text
carshow-frontend
carshow-backend
carshow-media
```

避让原则：

- 不修改 `/home/ubuntu/Carshow`。
- 不修改 Carshow 的 `docker-compose.yml`。
- 不接入 `carshow_default` 网络。
- 不停止、删除、重建 Carshow 容器。
- 不复用 Carshow 的端口、volume、配置文件。

## 4. 端口占用与避让

已被现有项目或系统占用/开放的端口：

```text
22      SSH
3000    Carshow frontend
8080    Carshow backend
1935    Carshow media
8189    Carshow media TCP/UDP
8888    Carshow media
8889    Carshow media
9997    Carshow media
123     NTP
53      systemd-resolved
```

审计时未发现监听：

```text
80
443
18080
18081
18082
```

本项目当前推荐：

```text
默认内部监听：127.0.0.1:18080
备用内部端口：127.0.0.1:18081 / 127.0.0.1:18082
```

临时公网调试：

```text
0.0.0.0:18080
```

注意：临时公网调试必须配置 `DEVICE_SHARED_SECRET`，并由用户手动确认腾讯云安全组和 UFW。本项目不默认长期裸露 18080。

## 5. Docker / Nginx / 防火墙状态

Docker：

- Docker 已安装并运行。
- Docker version：26.1.3
- Docker Compose：v2.27.1
- Docker Root Dir：`/var/lib/docker`
- Storage Driver：overlay2
- Logging Driver：json-file

Nginx：

- 宿主机未发现 `/etc/nginx`。
- Carshow frontend 容器内部有 Nginx。
- 当前不修改宿主机 Nginx，也不复用 Carshow 容器内 Nginx。

UFW：

- 状态：active
- 默认入站：deny
- 默认出站：allow
- 已开放：22、3000、8080、1935、8888
- 18080 是否开放由用户在临时公网调试时单独确认。

## 6. Python 环境

- Python：3.10.12
- pip：22.0.2
- `python3 -m venv` 可用

当前服务器项目采用：

```text
/home/ubuntu/esp-ai-terminal-server
/home/ubuntu/esp-ai-terminal-server/.venv
```

## 7. 当前服务器项目状态

项目目录：

```text
/home/ubuntu/esp-ai-terminal-server
```

当前阶段：S0.4

已验证接口：

```http
GET  /health
GET  /version
GET  /audit/runtime
POST /api/esp-ai-terminal/devices/register
POST /api/esp-ai-terminal/devices/heartbeat
GET  /api/esp-ai-terminal/devices/{device_id}/status
```

S0.4 实机验证结论：

- ESP32 已成功联网并获取 IP。
- ESP32 已携带 `X-Device-Token` 注册设备。
- 服务器返回 register `HTTP 200`。
- ESP32 已发送 heartbeat。
- 服务器返回 heartbeat `HTTP 200`。

关键日志：

```text
CLOUD: POST /api/esp-ai-terminal/devices/register token_configured=yes
CLOUD: device registered, http_status=200
CLOUD: POST /api/esp-ai-terminal/devices/heartbeat token_configured=yes
CLOUD: heartbeat ok, http_status=200
```

## 8. 风险与后续建议

风险：

- 服务器无 Swap，不适合本地运行大模型、ASR 或 TTS。
- 文件句柄限制 1024，后续 WebSocket 并发增加时要重新评估。
- journald 曾占用约 1.8G，后续服务日志必须限制输出量。
- 18080 公网裸露只适合短期调试。
- 当前设备鉴权是共享密钥方案，适合早期验证，不适合作为最终安全方案。

后续建议：

1. 正式公网访问前，单独进入 HTTPS 反向代理阶段。
2. 优先让服务继续监听 `127.0.0.1:18080`，由 Nginx/Caddy 反代 HTTPS。
3. 不在服务器运行大模型、本地 ASR、本地 TTS。
4. ASR/LLM/TTS API Key 只保存在服务器本地 `.env` 或后续独立密钥文件中。
5. 后续可将设备状态从内存字典迁移到 SQLite 或轻量文件存储。
