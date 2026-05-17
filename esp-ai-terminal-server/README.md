# ESP AI Terminal Server

这是 ESP32-S3 触屏 AI 语音终端的轻量服务器网关。当前阶段已完成设备管理、鉴权和心跳验证，S0.5 只新增服务器侧火山 ASR 配置检查与 smoke test，不接 ESP32 音频上传。

当前阶段：S0.5  
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
- S0.5 只允许服务器侧 ASR 自测，不接 LLM / TTS，不接 ESP32 音频上传
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

ASR 配置检查接口，不返回任何密钥原文：

```http
GET /api/esp-ai-terminal/asr/config
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
APP_STAGE=S0.5
APP_HOST=127.0.0.1
APP_PORT=18080
LOG_LEVEL=INFO
AI_PROVIDER=placeholder
DEVICE_SHARED_SECRET=CHANGE_ME

ASR_PROVIDER=volcengine
ASR_WS_URL=wss://openspeech.bytedance.com/api/v3/sauc/bigmodel_async
ASR_API_KEY=
ASR_APP_KEY=
ASR_ACCESS_KEY=
ASR_RESOURCE_ID=volc.seedasr.sauc.duration
ASR_AUDIO_FORMAT=pcm
ASR_SAMPLE_RATE=16000
ASR_BITS=16
ASR_CHANNELS=1
ASR_PACKET_MS=200

LLM_PROVIDER=volcengine_ark
ARK_API_KEY=
ARK_BASE_URL=https://ark.cn-beijing.volces.com/api/v3
LLM_MODEL=

TTS_PROVIDER=volcengine
TTS_MODEL=seed-tts-2.0
TTS_API_KEY=
TTS_VOICE_TYPE=
```

如果要让 ESP32 访问设备接口，请把 `DEVICE_SHARED_SECRET` 改成你自己生成的随机字符串。它不是火山引擎 API Key，而是 ESP32 到自建服务器之间的临时共享密钥。

生成示例：

```bash
openssl rand -hex 32
```

不要把真实密钥发到聊天，也不要提交到仓库。

火山 ASR 的 `ASR_API_KEY` 是另一套密钥，只能写入服务器本地 `.env`，不要写入 `.env.example`、README、聊天记录或 Git 仓库。

如果火山控制台给你的流式 ASR 凭证是 App Key / Access Key 形式，可以填写 `ASR_APP_KEY` 和 `ASR_ACCESS_KEY`。smoke test 会优先使用这组官方流式 Header；如果这两个字段为空，才使用 `ASR_API_KEY` 对应的 `X-Api-Key` 模式。

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

ASR 配置检查，确认是否已读取火山 ASR 参数。该接口只返回 Key 是否配置，不返回 Key 原文：

```bash
curl http://127.0.0.1:18080/api/esp-ai-terminal/asr/config
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

## 12. S0.5 火山 ASR 服务器侧自测

S0.5 只验证服务器能否用本地 `.env` 中的火山 ASR 配置连通云端 ASR。它不会接 ESP32 音频，不会接 LLM，不会接 TTS，也不会修改 Nginx / UFW / systemd / Docker / Carshow。

### 12.1 填写服务器本地 `.env`

在服务器上编辑：

```bash
cd /home/ubuntu/esp-ai-terminal-server
nano .env
```

至少确认这些字段：

```env
ASR_PROVIDER=volcengine
ASR_WS_URL=wss://openspeech.bytedance.com/api/v3/sauc/bigmodel_async
ASR_API_KEY=填写你的火山 ASR API Key
ASR_APP_KEY=
ASR_ACCESS_KEY=
ASR_RESOURCE_ID=volc.seedasr.sauc.duration
ASR_AUDIO_FORMAT=pcm
ASR_SAMPLE_RATE=16000
ASR_BITS=16
ASR_CHANNELS=1
ASR_PACKET_MS=200
```

`ASR_RESOURCE_ID=volc.seedasr.sauc.duration` 表示小时版资源，适合当前单设备开发调试。`.env` 已在 `.gitignore` 中，真实 Key 只应该留在服务器本地。

火山流式 ASR 凭证可能有两种形式：

- 如果你只有 API Key，填写 `ASR_API_KEY`。
- 如果控制台给的是 App Key / Access Key，填写 `ASR_APP_KEY` 和 `ASR_ACCESS_KEY`，脚本会优先使用这组官方流式鉴权 Header。

### 12.2 重启前台 Uvicorn

如果当前 Uvicorn 正在前台运行，先在运行窗口按：

```text
Ctrl+C
```

本机安全调试：

```bash
cd /home/ubuntu/esp-ai-terminal-server
. .venv/bin/activate
uvicorn app.main:app --host 127.0.0.1 --port 18080
```

临时公网调试仍可继续使用：

```bash
cd /home/ubuntu/esp-ai-terminal-server
. .venv/bin/activate
uvicorn app.main:app --host 0.0.0.0 --port 18080
```

`0.0.0.0:18080` 只是临时公网调试方式，不建议长期裸露。正式方案应单独进入 HTTPS 反向代理阶段，本轮不修改 Nginx、不修改 UFW、不修改云安全组。

### 12.3 测试 ASR 配置接口

```bash
curl http://127.0.0.1:18080/api/esp-ai-terminal/asr/config
```

期望看到：

```json
{
  "status": "ok",
  "provider": "volcengine",
  "configured": true,
  "api_key_configured": true
}
```

如果 `configured=false`，先检查 `.env` 是否缺少 `ASR_API_KEY`、`ASR_WS_URL` 或 `ASR_RESOURCE_ID`。

### 12.4 运行 ASR smoke test

```bash
cd /home/ubuntu/esp-ai-terminal-server
. .venv/bin/activate
python scripts/asr_smoke_test.py
```

脚本会生成 2 秒 `16kHz / 16bit / mono PCM` 测试音，按 200ms 分包，通过火山 ASR WebSocket 二进制协议发送。测试音可能识别不出有意义文本，这不一定代表链路失败；重点看是否出现鉴权错误、协议错误、WebSocket 连接错误和 `X-Tt-Logid`。

### 12.5 常见错误

- `ASR_API_KEY` 缺失：脚本会显示 `Config Missing`，请只在服务器本地 `.env` 填写。
- `ASR_APP_KEY / ASR_ACCESS_KEY` 与 `ASR_API_KEY` 混淆：如果 `X-Api-Key` 模式失败，请改用火山流式 ASR 文档对应的 App Key / Access Key。
- `ASR_RESOURCE_ID` 错误：可能返回鉴权或资源不可用错误，确认是否为 `volc.seedasr.sauc.duration`。
- WebSocket 连接失败：检查服务器出站网络、DNS、火山服务地址和 TLS。
- 火山鉴权失败：确认 `ASR_API_KEY` 是否属于火山 ASR 服务，且不要把设备的 `DEVICE_SHARED_SECRET` 当成火山 Key。
- 协议封包错误：查看脚本打印的服务端返回消息和错误码。
- `X-Tt-Logid`：如果服务端返回该字段，保留它用于火山控制台或工单排查。

## 13. 下一步建议

1. 不要长期裸露 `0.0.0.0:18080`。
2. 进入正式公网阶段前，优先做 HTTPS 反向代理。
3. S0.5 自测通过后，再进入 V0.6：ESP32 音频经自建服务器代理 ASR。
4. 后续再接 LLM / TTS 代理，真实云端 API Key 只放服务器本地 `.env`。
5. 后续可把设备状态从内存字典迁移到 SQLite。
