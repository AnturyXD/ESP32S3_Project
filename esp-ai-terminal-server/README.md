# ESP AI Terminal Server

这是 ESP32-S3 触屏 AI 语音终端的轻量服务器网关。当前阶段只做设备管理、鉴权和心跳验证，不接真实 ASR / LLM / TTS API。

当前阶段：S0.4  
默认内部端口：`127.0.0.1:18080`  
临时公网调试端口：`0.0.0.0:18080`，必须手动确认安全组和 UFW

## 1. 边界

本项目必须避让服务器上已有的 Carshow 项目。

禁止默认执行：

- 不修改 `/home/ubuntu/Carshow`
- 不修改 Nginx
- 不修改 UFW
- 不修改 systemd
- 不修改 Docker / Docker Compose
- 不占用 80 / 443
- 不接真实 ASR / LLM / TTS
- 不在仓库提交 `.env`、API Key、Token、密码

## 2. 当前接口

公开健康检查，不鉴权：

```http
GET /health
GET /version
GET /audit/runtime
```

设备接口，支持 `X-Device-Token`：

```http
POST /api/esp-ai-terminal/devices/register
POST /api/esp-ai-terminal/devices/heartbeat
GET  /api/esp-ai-terminal/devices/{device_id}/status
```

如果 `DEVICE_SHARED_SECRET` 为空或仍是 `CHANGE_ME`，开发阶段会临时放行设备接口并打印 warning。正式测试公网访问前必须配置真实共享密钥。

## 3. 本地打包

在仓库根目录执行：

```bash
bash esp-ai-terminal-server/scripts/local_pack.sh
```

生成：

```text
esp-ai-terminal-server/esp-ai-terminal-server.tar.gz
```

打包脚本会排除 `.env`、`.venv`、缓存、日志和打包产物。

## 4. 上传与解压

把 `YOUR_SERVER` 替换为真实服务器地址：

```bash
scp esp-ai-terminal-server/esp-ai-terminal-server.tar.gz ubuntu@YOUR_SERVER:/home/ubuntu/
```

服务器上执行：

```bash
mkdir -p /home/ubuntu/esp-ai-terminal-server
```

```bash
tar -xzf /home/ubuntu/esp-ai-terminal-server.tar.gz -C /home/ubuntu/esp-ai-terminal-server --strip-components=1
```

## 5. 创建虚拟环境并安装依赖

```bash
cd /home/ubuntu/esp-ai-terminal-server
```

```bash
python3 -m venv .venv
```

```bash
. .venv/bin/activate
```

```bash
pip install -r requirements.txt
```

如果当前终端没有激活 `.venv`，也可以直接使用：

```bash
/home/ubuntu/esp-ai-terminal-server/.venv/bin/python -m uvicorn app.main:app --host 127.0.0.1 --port 18080
```

## 6. 配置 `.env`

复制模板：

```bash
cp .env.example .env
```

编辑服务器本地 `.env`：

```bash
nano .env
```

最小配置：

```env
APP_NAME=esp-ai-terminal-server
APP_VERSION=0.1.0
APP_STAGE=S0.4
APP_HOST=127.0.0.1
APP_PORT=18080
LOG_LEVEL=INFO
AI_PROVIDER=placeholder
DEVICE_SHARED_SECRET=CHANGE_ME
ASR_API_KEY=
LLM_API_KEY=
TTS_API_KEY=
```

如果要让 ESP32 访问设备接口，请把 `DEVICE_SHARED_SECRET` 改成你自己生成的随机字符串。它不是火山引擎 API Key，而是 ESP32 到自建服务器之间的临时共享密钥。

生成示例：

```bash
openssl rand -hex 32
```

不要把真实密钥发到聊天，也不要提交到仓库。

## 7. 启动方式

### 方案 A：仅服务器本机访问，最安全

```bash
cd /home/ubuntu/esp-ai-terminal-server
. .venv/bin/activate
uvicorn app.main:app --host 127.0.0.1 --port 18080
```

特点：

- 只能服务器本机 curl。
- ESP32 不能直接访问。
- 不需要修改 UFW、云安全组、Nginx、systemd、Docker。

### 方案 B：临时公网调试，ESP32 可访问

```bash
cd /home/ubuntu/esp-ai-terminal-server
. .venv/bin/activate
uvicorn app.main:app --host 0.0.0.0 --port 18080
```

使用前必须确认：

- 腾讯云安全组已放行 18080/tcp。
- UFW 已允许 18080/tcp。
- `.env` 中 `DEVICE_SHARED_SECRET` 已配置为真实随机值。
- 这是短期调试方式，不建议长期裸露。

检查监听：

```bash
ss -ltnup | grep 18080
```

如果看到 `127.0.0.1:18080`，ESP32 不能访问。  
如果看到 `0.0.0.0:18080`，服务正在监听所有网卡。

### 方案 C：后续 HTTPS 反向代理

正式使用建议单独阶段实现：

- Nginx/Caddy 监听 443。
- FastAPI 继续监听 `127.0.0.1:18080`。
- 反向代理到本机端口。
- 本阶段不实现、不生成 Nginx 配置、不 reload 服务。

## 8. curl 验证

健康检查：

```bash
curl http://127.0.0.1:18080/health
```

版本：

```bash
curl http://127.0.0.1:18080/version
```

运行配置摘要，不能返回密钥原文：

```bash
curl http://127.0.0.1:18080/audit/runtime
```

无 token 注册请求。如果已配置真实 `DEVICE_SHARED_SECRET`，应返回 `401`：

```bash
curl -i -X POST http://127.0.0.1:18080/api/esp-ai-terminal/devices/register \
  -H "Content-Type: application/json" \
  -d '{"device_id":"esp32s3-dev-001","firmware_version":"V0.5.3","hardware":"ESP32-S3-Touch-LCD-3.49","ip":"192.168.1.100"}'
```

带 token 注册。把 `YOUR_DEVICE_SHARED_SECRET` 替换为服务器 `.env` 里的真实值：

```bash
curl -X POST http://127.0.0.1:18080/api/esp-ai-terminal/devices/register \
  -H "Content-Type: application/json" \
  -H "X-Device-Token: YOUR_DEVICE_SHARED_SECRET" \
  -d '{"device_id":"esp32s3-dev-001","firmware_version":"V0.5.3","hardware":"ESP32-S3-Touch-LCD-3.49","ip":"192.168.1.100"}'
```

带 token 心跳：

```bash
curl -X POST http://127.0.0.1:18080/api/esp-ai-terminal/devices/heartbeat \
  -H "Content-Type: application/json" \
  -H "X-Device-Token: YOUR_DEVICE_SHARED_SECRET" \
  -d '{"device_id":"esp32s3-dev-001","firmware_version":"V0.5.3","wifi_state":"Got IP","ip":"192.168.1.100","storage_state":"Ready","audio_state":"Ready","power_state":"Running"}'
```

查询设备状态：

```bash
curl http://127.0.0.1:18080/api/esp-ai-terminal/devices/esp32s3-dev-001/status \
  -H "X-Device-Token: YOUR_DEVICE_SHARED_SECRET"
```

## 9. ESP32 侧配置

ESP32 真实配置放在固件工程本地文件中：

```text
components/app_config/include/app_secrets.h
```

该文件已被 `.gitignore` 忽略，IDE 中显示灰色是正常的。

示例：

```cpp
#pragma once

#define WIFI_SSID "你的 Wi-Fi 名称"
#define WIFI_PASSWORD "你的 Wi-Fi 密码"

#define CLOUD_SERVER_BASE_URL "http://服务器公网IP:18080"
#define DEVICE_ID "esp32s3-dev-001"
#define DEVICE_SHARED_SECRET "和服务器 .env 相同的随机字符串"
```

如果只想验证 Wi-Fi，不测试云端：

```cpp
#define CLOUD_SERVER_BASE_URL ""
```

## 10. 停止与回滚

停止前台 Uvicorn：

```text
Ctrl+C
```

只读确认 18080 是否仍监听：

```bash
ss -ltnup | grep -E ':(18080)\b' || true
```

如需回滚，优先停止前台服务。删除目录前必须人工确认路径，禁止删除 Carshow：

```bash
# rm -rf /home/ubuntu/esp-ai-terminal-server
```

## 11. 当前验收结果

S0.4 已实机通过：

```text
WIFI: IP_EVENT_STA_GOT_IP, ip=192.168.31.6
CLOUD: POST /api/esp-ai-terminal/devices/register token_configured=yes
CLOUD: device registered, http_status=200
CLOUD: POST /api/esp-ai-terminal/devices/heartbeat token_configured=yes
CLOUD: heartbeat ok, http_status=200
```

结论：设备到服务器的最小安全通信链路已跑通。

## 12. 下一步建议

1. 不要长期裸露 `0.0.0.0:18080`。
2. 进入正式公网阶段前，优先做 HTTPS 反向代理。
3. 后续再接 ASR / LLM / TTS 代理，真实云端 API Key 只放服务器本地 `.env`。
4. 后续可把设备状态从内存字典迁移到 SQLite。
