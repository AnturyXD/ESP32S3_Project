# SERVER_COMMANDS.md

本文件只记录手动命令模板。命令不会修改 Nginx、UFW、Docker、systemd 或 Carshow。

## 1. 本地打包

用途：在本地生成服务器项目压缩包，不包含 `.env`、虚拟环境、缓存和日志。

```bash
bash esp-ai-terminal-server/scripts/local_pack.sh
```

## 2. 上传到服务器

用途：把压缩包上传到服务器用户目录。把 `YOUR_SERVER` 替换为真实服务器地址。

```bash
scp esp-ai-terminal-server/esp-ai-terminal-server.tar.gz ubuntu@YOUR_SERVER:/home/ubuntu/
```

## 3. 服务器解压

用途：解压到独立目录，避让 `/home/ubuntu/Carshow`。

```bash
mkdir -p /home/ubuntu/esp-ai-terminal-server
tar -xzf /home/ubuntu/esp-ai-terminal-server.tar.gz -C /home/ubuntu/esp-ai-terminal-server --strip-components=1
```

## 4. 只读检查

用途：确认 18080 端口状态，不修改任何服务。

```bash
ss -ltnup | grep -E ':(18080)\b' || true
```

## 5. Python 环境

用途：创建本项目独立虚拟环境，不影响系统 Python。

```bash
cd /home/ubuntu/esp-ai-terminal-server
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
```

## 6. 配置 `.env`

用途：填写服务器本地真实配置。`.env` 不应提交到仓库。

```bash
cd /home/ubuntu/esp-ai-terminal-server
cp .env.example .env
nano .env
```

V0.7 LLM 至少需要：

```env
ARK_API_KEY=<只填服务器本地真实火山方舟 API Key>
LLM_MODEL=<火山方舟推理接入点 ID>
```

## 7. 前台启动

用途：本机安全调试，只允许服务器本机访问。

```bash
cd /home/ubuntu/esp-ai-terminal-server
. .venv/bin/activate
uvicorn app.main:app --host 127.0.0.1 --port 18080
```

用途：临时公网调试，ESP32 可访问。执行前必须确认云安全组、UFW 和 `DEVICE_SHARED_SECRET`。

```bash
cd /home/ubuntu/esp-ai-terminal-server
. .venv/bin/activate
uvicorn app.main:app --host 0.0.0.0 --port 18080
```

## 8. 接口测试

用途：健康检查。

```bash
curl http://127.0.0.1:18080/health
```

用途：检查 LLM 配置，不返回 `ARK_API_KEY` 原文。

```bash
curl http://127.0.0.1:18080/api/esp-ai-terminal/llm/config
```

用途：测试 Chat 接口。把 `<DEVICE_SHARED_SECRET>` 替换为服务器 `.env` 的设备共享密钥，不要写入文档或仓库。

```bash
curl -X POST http://127.0.0.1:18080/api/esp-ai-terminal/chat \
  -H "Content-Type: application/json" \
  -H "X-Device-Token: <DEVICE_SHARED_SECRET>" \
  -d '{"device_id":"esp32s3-dev-001","text":"请用一句话介绍你自己","source":"manual","language":"zh"}'
```

## 9. 停止

用途：停止前台 Uvicorn。

```text
Ctrl+C
```

用途：只读确认端口是否仍监听。

```bash
ss -ltnup | grep -E ':(18080)\b' || true
```
