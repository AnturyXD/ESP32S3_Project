# AGENTS.md

## 角色

你是本项目的 Codex / AI 编程代理，负责开发一款基于 Waveshare ESP32-S3-Touch-LCD-3.49 的触屏 AI 语音终端，并逐步接入 SD 卡外部存储和自建服务器网关。

本项目不是一次性 Demo，而是一个按版本演进的嵌入式产品工程。你必须严格遵守模块边界、版本范围、构建验证和安全规则。

---

## 总目标

设备端目标：

1. 使用 ESP-IDF + LVGL 开发触屏设备。
2. 支持 Home、AI Voice、Settings、Debug、About 等页面。
3. 支持 Wi-Fi、时间同步、PWR 电池供电控制、音频输入输出。
4. 使用 SD 卡作为外部存储，缓解内部存储压力。
5. 后续通过自建服务器代理 ASR / LLM / TTS，实现类似豆包电话的语音交互。

服务器端目标：

1. 在已有其他项目运行的 2 核 4G 6M Linux 服务器上，避让式部署轻量网关。
2. 网关只做设备管理、鉴权、日志、ASR/LLM/TTS 代理。
3. 不在服务器上运行大模型、ASR 模型或 TTS 模型。
4. 不破坏服务器已有服务、端口、Nginx、Docker、数据库和项目目录。

---

## 硬性规则

### 设备端硬性规则

1. 不要把所有逻辑写进 `main/app_main.cpp`。
2. `app_main.cpp` 只做模块初始化流程。
3. UI 页面不能直接调用底层驱动、Wi-Fi、I2S、SD 卡、云端 API。
4. 音频任务不能直接操作 LVGL。
5. Wi-Fi / Time / ASR / WebSocket 回调不能直接操作 LVGL。
6. 所有 LVGL 控件更新必须在 UI task / LVGL mutex / LVGL timer 安全上下文中执行。
7. 不允许在内部 Flash 中保存大块音频、长期日志、临时缓存。
8. SD 卡缺失时设备必须降级运行，不能崩溃。
9. 不提交真实 Wi-Fi 密码、API Key、Token。
10. 不打印 Wi-Fi 密码和完整 API Key。

### TCA9554 / EXIO 规则

1. TCA9554 是共享 IO 扩展芯片，不能由多个模块各自直接写寄存器。
2. EXIO6 = SYS_EN，用于电池供电保持。
3. EXIO7 = speaker/audio path enable，用于扬声器或音频通路使能。
4. 必须通过统一的 `bsp_io_expander` 或等价共享层管理 TCA9554。
5. 必须维护 `output_shadow` 和 `direction_shadow`。
6. 设置某一个 EXIO 时，只能修改对应 bit，不能覆盖其他 bit。
7. I2C 写 TCA9554 必须加 mutex。
8. service_power 和 service_audio 不允许各自直接操作 TCA9554 寄存器。

### 中文注释规则

以后新增或修改代码时，必须尽可能写完整中文注释。

要求：

1. 模块头文件写中文说明。
2. 关键函数写中文函数注释。
3. 状态机分支写中文解释。
4. 硬件相关逻辑必须写清楚硬件原因。
5. 云端接口、鉴权、WebSocket 状态必须写中文说明。
6. 不要写无意义注释，例如“定义变量”“调用函数”。
7. 注释要解释“为什么这样做”。

示例：

```cpp
/**
 * @brief 将单声道 PCM 转换为左右声道相同的立体声 PCM
 *
 * 当前板载 ES8311 播放链路使用 I2S 立体声槽位。
 * 如果直接写入单声道数据，可能出现 esp_codec_dev_write 成功但扬声器无声的问题。
 * 因此播放前需要将 mono PCM 复制到 L/R 两个声道。
 */
```

---

## 当前版本路线

必须按以下顺序推进：

```text
V0.5.2    TCA9554 共享 IO 状态保护修复：PASS
V0.5.3    SD 卡外部存储接入：PASS / 基本正常
S0.1      服务器只读审计与部署计划：PASS
S0.2-pre  本地 esp-ai-terminal-server/ 服务端骨架：PASS
S0.3      服务器最小运行 + 注册/心跳接口：PASS
S0.4      ESP32 真机注册与周期心跳上报：PASS

S0.5      服务器侧火山 ASR 配置、接口与 smoke test
V0.6      ESP32 音频 → 服务器 → 火山 ASR → 屏幕显示识别文字
V0.7      LLM 文本对话代理
V0.8      TTS 代理与设备播放
V0.9      AI Voice 状态机稳定性
V1.0      MVP 演示版
```

不要跳步。

---

## 设备端组件职责

必须保持以下职责边界：

```text
app_config        配置占位、编译期配置、secrets 引用
bsp_board         LCD / Touch / Audio 基础板级初始化
bsp_io_expander   TCA9554 共享 IO 管理
service_power     PWR / SYS_OUT / SYS_EN 电源状态管理
service_network   Wi-Fi STA、IP、重连状态
service_time      SNTP、系统时间格式化
service_audio     录音、播放、音频状态
service_storage   SD 卡挂载、目录、文件、日志落盘
service_cloud     设备连接自建服务器、注册、心跳、配置拉取
service_ai        ASR / LLM / TTS 状态机协调
service_log       串口日志、SD 日志缓存
ui                LVGL 初始化、页面、路由、状态刷新
app_shell         App 注册、页面 ID、路由元信息
```

禁止跨层：

1. UI 页面直接调用 I2S。
2. UI 页面直接调用 esp_wifi。
3. UI 页面直接操作 SD 卡文件。
4. service_audio 直接操作 LVGL。
5. service_power 直接操作 LVGL。
6. service_ai 直接写页面控件。
7. app_shell 写具体 LVGL 控件。

---

## SD 卡开发规则

开发 `service_storage` 时必须遵守：

1. 挂载点统一为 `/sdcard`。
2. 标准目录必须由 `service_storage_ensure_dirs()` 创建。
3. SD 卡不存在时状态为 `STORAGE_STATE_NO_CARD`，系统继续运行。
4. FATFS 挂载失败要打印错误，不允许反复重启。
5. 日志写入 SD 卡必须做轮转。
6. 音频缓存必须做大小限制。
7. 不允许把音频调试文件写到内部 Flash。
8. Debug 页面可以显示 SD 状态、容量、剩余空间。
9. 任何文件写入失败都必须优雅降级。

标准目录：

```text
/sdcard/logs
/sdcard/audio/record_cache
/sdcard/audio/asr_upload
/sdcard/audio/tts_cache
/sdcard/audio/debug
/sdcard/config
/sdcard/ota/packages
/sdcard/crash
/sdcard/tmp
```

---

## 服务器开发规则

### 服务器必须先审计

在 2 核 4G 6M Linux 服务器上开发前，必须先运行只读审计命令，并生成 `SERVER_AUDIT.md`。

只读审计命令：

```bash
hostname
whoami
pwd
uname -a
free -h
df -h
ss -lntup
systemctl --type=service --state=running --no-pager
docker ps --format 'table {{.Names}}\t{{.Image}}\t{{.Ports}}\t{{.Status}}'
docker compose ls 2>/dev/null || true
nginx -T 2>/dev/null | head -n 80 || true
ls -la /etc/nginx/sites-enabled 2>/dev/null || true
ls -la /opt 2>/dev/null || true
ls -la /var/www 2>/dev/null || true
crontab -l 2>/dev/null || true
```

审计前禁止安装、删除、重启、修改任何服务。

### 服务器避让规则

禁止：

1. 未确认就占用 80 / 443。
2. 未确认就修改 Nginx 主配置。
3. 未确认就重启 Nginx。
4. 未确认就停止已有项目。
5. 未确认就删除 Docker 容器或 volume。
6. 未确认就执行 `apt upgrade`。
7. 未确认就修改防火墙规则。
8. 未确认就使用已有项目数据库。
9. 未确认就写入 `/var/www` 现有目录。

推荐：

```text
项目目录：/opt/esp32-ai-gateway
数据目录：/var/lib/esp32-ai-gateway
日志目录：/var/log/esp32-ai-gateway
密钥文件：/etc/esp32-ai-gateway/gateway.env
运行用户：esp32ai
默认端口：127.0.0.1:18080
服务名：esp32-ai-gateway.service
```

### 服务器技术栈

默认使用：

```text
Python 3.11+
FastAPI
Uvicorn
WebSocket
SQLite（可选）
systemd
```

如果服务器已有 Docker 体系，可以改用 Docker Compose，但必须使用独立 project name、独立 network、独立 volume，不得复用已有项目资源。

### 服务器最小接口

S0.2 至少实现：

```http
GET /health
```

S0.3 增加：

```http
POST /api/v1/device/register
POST /api/v1/device/heartbeat
GET  /api/v1/device/config
```

V0.6 增加：

```text
WS /ws/asr
```

V0.7 增加：

```http
POST /api/v1/chat
```

V0.8 增加：

```text
WS /ws/tts
```

---

## 密钥与配置规则

设备端：

```text
app_config.h          可提交，占位配置
app_secrets.h         不提交，真实本地配置
app_secrets.h.example 可提交，字段说明
```

服务器端：

```text
.env.example                         可提交
.env                                 不提交
/etc/esp32-ai-gateway/gateway.env     服务器真实配置，不进仓库
```

必须加入 `.gitignore`：

```text
app_secrets.h
.env
*.pem
*.key
*.crt
```

禁止提交：

1. Wi-Fi 密码。
2. 豆包 / 火山 API Key。
3. device_token。
4. admin token。
5. 私钥文件。

---

## 每轮任务执行流程

每次开始任务前：

1. 阅读 `PROJECT_SPEC.md`。
2. 阅读 `AGENTS.md`。
3. 明确当前版本号。
4. 明确本轮禁止做什么。
5. 先说明计划，再修改。

每次完成后：

1. 列出修改文件。
2. 说明是否遵守版本范围。
3. 说明关键架构边界是否保持。
4. 运行构建或服务启动验证。
5. 汇报 warning 和风险。
6. 说明下一步注意事项。

设备端构建：

```bash
idf.py build
```

如果 `idf.py` 不在 PATH，使用项目已验证的 ESP-IDF Python 直调方式。

服务器端验证：

```bash
python -m compileall app
curl http://127.0.0.1:18080/health
```

---

## 当前优先任务提示词

### V0.5.2 提示词

```text
当前进入 V0.5.2：TCA9554 共享 IO 状态保护修复。

V0.4.5 PWR 电池供电实机通过，但 V0.5 为修复测试音新增 EXIO7 speaker enable 后，电池供电保持失效。请统一 TCA9554 访问入口，新增或完善 bsp_io_expander，维护 output_shadow / direction_shadow，并使用 mutex 保护 I2C 写操作。

要求：
1. EXIO6 / SYS_EN 和 EXIO7 / speaker enable 必须能同时 HIGH。
2. service_power 不再直接写 TCA9554。
3. service_audio 不再直接写 TCA9554。
4. service_power 通过 bsp_io_expander 设置 EXIO6。
5. service_audio 通过 bsp_io_expander 设置 EXIO7。
6. 修复后电池供电长按 PWR 启动，松手不掉电。
7. Play Test Tone 仍然有声音。
8. 长按 PWR 仍然能关机。
9. build 通过。
10. 新增或修改代码必须尽可能写中文注释。
```

### V0.5.3 提示词

```text
当前进入 V0.5.3：SD 卡外部存储接入。

本轮只做 service_storage 和 SD 卡挂载，不实现 ASR/LLM/TTS。

要求：
1. 新增 service_storage 组件。
2. 使用 ESP-IDF FATFS + SDMMC/SDSPI 方式挂载 SD 卡，具体接口参考当前开发板官方示例和 ESP-IDF SD 卡示例。
3. 挂载点为 /sdcard。
4. 创建标准目录 logs/audio/config/ota/crash/tmp。
5. Debug 页面显示 SD 状态、总容量、剩余容量。
6. service_log 可选写入 SD 卡日志。
7. SD 卡缺失时系统继续运行，不崩溃。
8. 不把大文件写入内部 Flash。
9. 代码写中文注释。
10. build 通过。
```

### S0.1 提示词

```text
当前进入 S0.1：服务器只读审计与部署计划。

服务器规格为 2 核 4G 内存 6M 带宽 Linux，已有其他项目运行。你只能做只读审计，不能安装、删除、重启或修改任何服务。

请执行 PROJECT_SPEC.md 中的只读审计命令，生成 SERVER_AUDIT.md，列出端口、服务、Docker、Nginx、磁盘、内存、风险和推荐部署方案。
```

---

## 最终提醒

1. 先修 TCA9554，再接 SD 卡，再开始服务器。
2. 服务器先审计，不要直接部署。
3. 服务器只做轻量代理，不跑模型。
4. 后续 ASR / LLM / TTS 都经服务器代理。
5. 代码必须尽可能写清楚中文注释。
6. 不要提交真实密钥。


---

## S0.4 / S0.5 最新项目补充（2026-05-18）

### S0.4 当前结论

S0.4 已通过实机验证，当前可以记录为：

```text
S0.4 PASS：ESP32 → 自建 FastAPI 服务器的注册与周期心跳链路实机通过
```

已确认能力：

1. ESP32 已通过 Wi-Fi 成功获取 IP。
2. ESP32 可以访问自建 FastAPI 服务器。
3. `POST /api/esp-ai-terminal/devices/register` 返回 `http_status=200`。
4. `POST /api/esp-ai-terminal/devices/heartbeat` 返回 `http_status=200`。
5. 服务器端 `DEVICE_SHARED_SECRET_CONFIGURED=true`，设备鉴权已启用。
6. 设备端串口显示 `token_configured=yes`，说明设备密钥与服务器密钥匹配。
7. `service_cloud` 已作为独立组件接入。
8. UI 没有直接调用 HTTP / 云端 API。
9. HTTP 请求没有阻塞 UI task。
10. 云端状态通过 Debug 页面只读显示。
11. `app_main.cpp` 仍只负责模块初始化。
12. 服务器端仍未接真实 ASR / LLM / TTS。
13. 未修改 Carshow、Nginx、Docker、systemd。
14. 真实 Wi-Fi、服务器地址、设备密钥放在本地 `app_secrets.h` / 服务器 `.env`，未提交到仓库。

S0.4 实机日志基线：

```text
WIFI: IP_EVENT_STA_GOT_IP, ip=192.168.31.6
CLOUD: POST /api/esp-ai-terminal/devices/register token_configured=yes
CLOUD: device registered, http_status=200
CLOUD: POST /api/esp-ai-terminal/devices/heartbeat token_configured=yes
CLOUD: heartbeat ok, http_status=200
```

### 服务器访问方式补充

当前如果服务器使用 `0.0.0.0:18080` 并已放行腾讯云安全组，这是临时公网调试方式。

必须遵守：

1. 不建议长期裸露 `18080`。
2. 临时公网调试时必须启用 `X-Device-Token`。
3. 后续正式使用应改为 HTTPS 反向代理或更安全的访问方案。
4. 不允许未审批就修改 Nginx。
5. 不允许未审批就修改 UFW。
6. 不允许未审批就写 systemd。
7. 不允许未审批就改 Docker。
8. 不允许影响 Carshow。

推荐方向：

```text
短期调试：0.0.0.0:18080 + X-Device-Token + 云安全组临时放行
正式建议：HTTPS 反向代理 → 127.0.0.1:18080
```

### 当前服务器 API 规范

当前已实现：

```http
GET  /health
GET  /version
GET  /audit/runtime

POST /api/esp-ai-terminal/devices/register
POST /api/esp-ai-terminal/devices/heartbeat
GET  /api/esp-ai-terminal/devices/{device_id}/status
```

设备接口使用请求头：

```http
X-Device-Token: <DEVICE_SHARED_SECRET>
```

规则：

1. `/health`、`/version`、`/audit/runtime` 可以不鉴权，但不能泄露密钥。
2. `/api/esp-ai-terminal/devices/*` 必须支持 `X-Device-Token` 鉴权。
3. 日志不能打印 token 原文。
4. `DEVICE_SHARED_SECRET` 是 S0.4 临时共享密钥方案，后续可升级为设备证书、JWT 或短期 token。

### S0.5：服务器侧火山 ASR 接入与自测

当前下一阶段是：

```text
S0.5：服务器侧火山 ASR 配置、接口与 smoke test
```

本轮只做服务器侧 ASR 配置和自测，不修改 ESP32 固件，不接 LLM，不接 TTS。

S0.5 必须新增或更新服务器 `.env.example`：

```env
ASR_PROVIDER=volcengine
ASR_WS_URL=wss://openspeech.bytedance.com/api/v3/sauc/bigmodel_async
ASR_API_KEY=
ASR_RESOURCE_ID=volc.seedasr.sauc.duration
ASR_AUDIO_FORMAT=pcm
ASR_SAMPLE_RATE=16000
ASR_BITS=16
ASR_CHANNELS=1
ASR_PACKET_MS=200
```

S0.5 必须新增配置检查接口：

```http
GET /api/esp-ai-terminal/asr/config
```

允许返回：

1. provider
2. configured
3. ws_url
4. resource_id
5. audio_format
6. sample_rate
7. bits
8. channels
9. packet_ms
10. api_key_configured: true/false

禁止返回：

1. API Key 原文。
2. Token 原文。
3. Password 原文。

S0.5 必须新增：

```text
scripts/asr_smoke_test.py
```

功能要求：

1. 从服务器本地 `.env` 读取 ASR 配置。
2. 检查 `ASR_API_KEY`、`ASR_WS_URL`、`ASR_RESOURCE_ID`。
3. 生成 1~2 秒 16kHz / 16bit / mono PCM 测试音或静音。
4. 通过 WebSocket 连接火山 ASR。
5. 使用 Header：
   - `X-Api-Key`
   - `X-Api-Resource-Id`
   - `X-Api-Request-Id`
   - `X-Api-Sequence`
6. 按 200ms 分包发送音频。
7. 打印连接状态、返回消息、错误码、`X-Tt-Logid`。
8. 打印已发送音频秒数和估算计费时长。
9. 不打印 ASR API Key 原文。
10. 火山 ASR WebSocket 使用二进制协议，不允许用普通文本 WebSocket 冒充成功。

S0.5 验收标准：

```text
[ ] /api/esp-ai-terminal/asr/config 返回 configured=true
[ ] /api/esp-ai-terminal/asr/config 不返回 ASR_API_KEY 原文
[ ] asr_smoke_test.py 能读取服务器 .env
[ ] 能连接火山 ASR WebSocket，或返回明确可排查错误
[ ] 能打印 X-Tt-Logid
[ ] 能统计发送音频秒数
[ ] 不修改 ESP32 固件
[ ] 不影响设备注册与心跳
[ ] 不影响 Carshow
```

### 火山引擎模型选择补充

本项目采用模块化云端 API 代理方案：

```text
ESP32-S3
  ↓
自建 FastAPI 服务器
  ↓
火山引擎：
  ASR：Doubao-流式语音识别
  LLM：火山方舟在线推理（常规）
  TTS：Doubao-语音合成-2.0
```

ASR 当前选择：

```text
产品：豆包语音
能力：Doubao-流式语音识别
接口：大模型流式语音识别 API
推荐地址：wss://openspeech.bytedance.com/api/v3/sauc/bigmodel_async
资源 ID：volc.seedasr.sauc.duration
音频格式：16kHz / 16bit / mono PCM
分包建议：200ms
```

当前不要选择：

1. Doubao-实时语音交互。
2. 火山方舟在线推理低延迟版。
3. TPM 保障包。
4. 自定义模型部署。
5. 声音复刻。
6. ESP32 直连火山引擎 API。

### S0.5 给 Codex 的提示词

```text
当前进入 S0.5：服务器侧火山 ASR 接入与自测。

当前状态：
1. S0.4 已通过。
2. ESP32 可以通过 Wi-Fi 访问自建 FastAPI 服务器。
3. 设备注册接口返回 200。
4. 设备心跳接口返回 200。
5. DEVICE_SHARED_SECRET 已启用。
6. X-Device-Token 鉴权已通过。
7. service_cloud 已作为 ESP32 独立组件接入。
8. 服务器端仍未接真实 ASR / LLM / TTS。
9. 本轮只做服务器侧火山 ASR 接入与自测。
10. 本轮不修改 ESP32 固件。
11. 本轮不接 LLM。
12. 本轮不接 TTS。
13. 本轮不修改 Carshow、Nginx、Docker、systemd。
14. 本轮不把 API Key、Token、密码写入仓库。
15. 所有新增代码必须尽可能写完整中文注释。

请完成：
1. 更新 .env.example，增加火山 ASR 占位配置。
2. app/core/config.py 增加 ASR 配置项。
3. 新增 GET /api/esp-ai-terminal/asr/config。
4. 新增 scripts/asr_smoke_test.py。
5. smoke test 使用官方二进制 WebSocket 协议。
6. 不泄露 ASR_API_KEY。
7. 文档补充服务器 .env 配置和测试命令。
8. 不影响设备注册与心跳。
```

### V0.6 预告

S0.5 通过后再进入 V0.6：

```text
V0.6：ESP32 音频 → 服务器 → 火山 ASR → 屏幕显示识别文字
```

V0.6 前置条件：

1. S0.5 已通过。
2. 服务器 ASR 配置 complete。
3. `asr_smoke_test.py` 能连接火山 ASR 或返回明确可排查错误。
4. ESP32 `service_audio` 录音链路可输出 16kHz / 16bit / mono PCM。
5. ESP32 `service_cloud` 已能访问服务器。
6. ESP32 不保存火山 API Key。

V0.6 目标：

1. 服务器新增设备侧 ASR 上传接口或 WebSocket。
2. ESP32 AI Voice 页面 Start ASR 按钮触发录音与上传。
3. 服务器代理火山 ASR。
4. 返回 partial / final text。
5. AI Voice 页面显示识别文本。
6. 限制最长录音时长，避免持续计费。
7. 静音或停止按钮后结束 ASR。
