# AGENTS.md

## 1. 文档职责

你是本项目的 Codex / AI 编程代理，负责协助开发基于 **Waveshare ESP32-S3-Touch-LCD-3.49** 的触屏 AI 语音终端。

本文件只写 **规范、边界、职责、安全要求、禁止事项和通用验收规则**。具体版本步骤、阶段计划、提示词和验收清单放在 `PROJECT_SPEC.md`。两份文件必须各司其职：

- `AGENTS.md`：告诉 Codex “必须遵守什么规则”。
- `PROJECT_SPEC.md`：告诉 Codex “每个版本具体做什么”。

每次开发前必须先阅读 `PROJECT_SPEC.md` 和 `AGENTS.md`。

---

## 2. 项目目标

### 2.1 设备端目标

1. 使用 ESP-IDF + LVGL 开发触屏设备。
2. 支持 Home、AI Voice、Settings、Debug、About 等页面。
3. 支持 Wi-Fi、时间同步、PWR 电池供电控制、音频输入输出、SD 卡外部存储。
4. 通过自建服务器代理 ASR / LLM / TTS，实现类似豆包电话的语音交互。
5. 设备屏幕只作为状态和调试辅助显示，不作为中文内容主输出通道。
6. 最终用户体验目标是：**用户用中文说话，设备能听懂，并用中文语音回答**。

### 2.2 服务器端目标

1. 在已有其他项目运行的 2 核 / 4G / 6M Linux 服务器上避让式部署轻量网关。
2. 服务器只做设备管理、鉴权、日志、ASR / LLM / TTS 代理。
3. 不在服务器上本地运行大模型、ASR 模型或 TTS 模型。
4. 不破坏服务器已有服务、端口、Nginx、Docker、systemd、数据库和项目目录。
5. 真实火山引擎密钥只保存在服务器本地 `.env`，不写入 ESP32 固件，不提交仓库。

---

## 3. 当前项目基线

```text
V0.1      工程骨架：PASS
V0.2      LCD / Touch / LVGL / Home：PASS
V0.3      App Shell / 页面路由：PASS
V0.3.x    返回键与页面切换花屏修复：PASS
V0.4      Wi-Fi / Time / 状态刷新：PASS
V0.4.5    PWR 电池供电控制：PASS
V0.5      Audio I/O 基础验证：PASS
V0.5.2    TCA9554 EXIO6 / EXIO7 共享状态保护：PASS
V0.5.3    SD 卡外部存储：PASS / 基本正常
S0.1      服务器只读审计与部署计划：PASS
S0.2-pre  本地 esp-ai-terminal-server/ 服务端骨架：PASS
S0.3      服务器最小运行 + 注册/心跳接口：PASS
S0.4      ESP32 真机注册与周期心跳上报：PASS
S0.5      服务器侧火山 ASR WebSocket smoke test：PASS
V0.6      ESP32 音频 → 服务器 → 火山 ASR → 识别结果返回设备：PASS / 主链路跑通
```

当前下一阶段：

```text
V0.6.1    ASR 稳定性收尾，不做中文显示
V0.7      LLM 文本对话代理
V0.8      TTS 中文语音播放
V0.9      AI Voice 状态机稳定性
V1.0      MVP 演示版
```

---

## 4. 总体架构原则

```text
ESP32-S3 设备
  ↓ 设备鉴权 / 音频流 / 状态上报
自建 FastAPI 服务器
  ↓ 服务端保存真实云服务密钥
火山引擎 / 豆包语音 / 火山方舟
```

必须保持：

1. ESP32 不直接保存火山 ASR / LLM / TTS 密钥。
2. ESP32 只保存 Wi-Fi、服务器地址和设备共享密钥。
3. 自建服务器负责调用第三方云 API。
4. 服务器对外接口必须有鉴权和日志脱敏。
5. 设备端 UI 不直接操作网络、音频、SD 卡、云端 API。

---

## 5. 中文显示与中文语音输出规范

### 5.1 屏幕中文显示策略

本项目后续 **不要求 ESP32 屏幕正确显示中文内容**。

原因：

1. LVGL 显示中文需要字体文件包含中文字形。
2. 引入完整中文字库会明显增加固件体积和维护复杂度。
3. 本项目最终目标是语音交互，中文内容应通过 TTS 播放，而不是依赖屏幕阅读。
4. 屏幕只承担状态显示、调试信息、英文/ASCII 提示和数字信息。

### 5.2 UI 降级显示规则

如果 ASR / LLM 返回中文文本：

1. 串口日志和服务器日志可以保留完整中文，便于调试。
2. UI 页面不要求显示中文原文。
3. UI 可以显示英文状态，例如：
   - `Chinese text received`
   - `Response received`
   - `Reply ready`
   - `TTS playing`
   - `ASR done`
4. 不允许为了显示中文而引入大体积中文字库，除非用户后续明确要求。

### 5.3 中文输出验收位置

中文输出能力的主要验收版本是：

```text
V0.8：TTS 中文语音播放
```

V0.8 的核心验收不是“屏幕显示中文”，而是：

```text
LLM 中文回复 → 火山 TTS → ESP32 播放中文语音
```

---

## 6. 设备端硬性规则

1. `main/app_main.cpp` 只做模块初始化流程，不承载业务逻辑。
2. UI 页面不能直接调用底层驱动、Wi-Fi、I2S、SD 卡、HTTP、WebSocket、云端 API。
3. 音频任务不能直接操作 LVGL。
4. Wi-Fi / Time / ASR / WebSocket 回调不能直接操作 LVGL。
5. 所有 LVGL 控件更新必须在 UI task / LVGL mutex / LVGL timer 安全上下文中执行。
6. 不允许在内部 Flash 中保存大块音频、长期日志、临时缓存。
7. SD 卡缺失时设备必须降级运行，不能崩溃。
8. 不提交真实 Wi-Fi 密码、服务器地址、API Key、Token、设备密钥。
9. 不打印 Wi-Fi 密码、完整 API Key、完整 Token。
10. HTTP / WebSocket 请求不能阻塞 UI task。
11. Start ASR 必须由用户触发，不能一进入页面就持续上传音频。
12. ASR / TTS 必须有单次最大时长限制，避免无意义计费。
13. Stop ASR 后必须停止音频采集和上传。
14. 设备端不直接连接火山 ASR / LLM / TTS API。

---

## 7. 设备端组件职责

```text
app_config        配置占位、编译期配置、secrets 引用
bsp_board         LCD / Touch / Audio 基础板级初始化
bsp_io_expander   TCA9554 共享 IO 管理
service_power     PWR / SYS_OUT / SYS_EN 电源状态管理
service_network   Wi-Fi STA、IP、重连状态
service_time      SNTP、系统时间格式化
service_audio     录音、播放、音频状态
service_storage   SD 卡挂载、目录、文件、日志落盘
service_cloud     设备连接自建服务器、注册、心跳、配置拉取、ASR/LLM/TTS 通道
service_ai        ASR / LLM / TTS 状态机协调
service_log       串口日志、SD 日志缓存
ui                LVGL 初始化、页面、路由、状态刷新
app_shell         App 注册、页面 ID、路由元信息
```

禁止跨层：

1. UI 页面直接调用 I2S。
2. UI 页面直接调用 `esp_wifi`。
3. UI 页面直接操作 SD 卡文件。
4. UI 页面直接发 HTTP / WebSocket。
5. `service_audio` 直接操作 LVGL。
6. `service_power` 直接操作 LVGL。
7. `service_ai` 直接写页面控件。
8. `app_shell` 写具体 LVGL 控件。
9. 服务器模块直接写设备端本地 secrets。

---

## 8. TCA9554 / EXIO 规则

1. TCA9554 是共享 IO 扩展芯片，不能由多个模块各自直接写寄存器。
2. EXIO6 = SYS_EN，用于电池供电保持。
3. EXIO7 = speaker/audio path enable，用于扬声器或音频通路使能。
4. 必须通过统一的 `bsp_io_expander` 或等价共享层管理 TCA9554。
5. 必须维护 `output_shadow` 和 `direction_shadow`。
6. 设置某一个 EXIO 时，只能修改对应 bit，不能覆盖其他 bit。
7. I2C 写 TCA9554 必须加 mutex。
8. `service_power` 和 `service_audio` 不允许各自直接操作 TCA9554 寄存器。
9. 修改 EXIO6 / EXIO7 后必须同时回归：电池供电、Play Test Tone、长按 PWR 关机。

---

## 9. SD 卡开发规则

1. 挂载点统一为 `/sdcard`。
2. 目录必须由 `service_storage_ensure_dirs()` 或等价函数统一创建。
3. SD 卡不存在时状态为 `STORAGE_STATE_NO_CARD`，系统继续运行。
4. FATFS 挂载失败要打印错误，不允许反复重启。
5. 日志写入 SD 卡必须做轮转。
6. 音频缓存必须做大小限制。
7. 不允许把音频调试文件写到内部 Flash。
8. Debug 页面可以显示 SD 状态、容量、剩余空间。
9. 任何文件写入失败都必须优雅降级。
10. 默认不自动格式化用户 SD 卡。
11. 默认不启用长文件名作为强依赖。
12. 推荐使用 FAT32 + MBR 单分区。
13. 8GB / 16GB / 32GB SDHC 作为基础验收卡。
14. 64GB / 128GB SDXC 可尝试，但建议手动格式化为 FAT32 + MBR 单分区。
15. 不保证 exFAT / NTFS / GPT / 多分区 / 出厂特殊分区卡可用。

推荐短文件名目录：

```text
/sdcard/EAITERM
/sdcard/EAITERM/LOGS
/sdcard/EAITERM/AUDIO
/sdcard/EAITERM/ASR
/sdcard/EAITERM/TTS
/sdcard/EAITERM/CFG
/sdcard/EAITERM/OTA
/sdcard/EAITERM/TMP
```

路径常量必须集中定义，不允许硬编码散落在多个文件中。

---

## 10. 服务器开发规则

### 10.1 服务器避让

服务器已有其他项目运行，所有服务器部署必须先做只读审计。禁止未确认就：

1. 安装软件。
2. 删除文件。
3. 重启服务。
4. 修改 Nginx。
5. 修改 Docker。
6. 修改 systemd。
7. 修改 UFW / 云安全组。
8. 修改已有项目目录。
9. 占用 80 / 443。
10. 长期裸露 18080。

当前临时调试策略：

```text
短期调试：0.0.0.0:18080 + X-Device-Token + 云安全组临时放行
正式建议：HTTPS 反向代理 → 127.0.0.1:18080
```

### 10.2 服务器技术栈

```text
Python 3.11+
FastAPI
Uvicorn
WebSocket
python-dotenv / pydantic-settings
SQLite（后续可选）
```

服务器只做轻量代理，不跑模型。

### 10.3 当前服务器 API 规范

当前已实现：

```http
GET  /health
GET  /version
GET  /audit/runtime

POST /api/esp-ai-terminal/devices/register
POST /api/esp-ai-terminal/devices/heartbeat
GET  /api/esp-ai-terminal/devices/{device_id}/status

GET  /api/esp-ai-terminal/asr/config
WS   /ws/esp-ai-terminal/asr
```

后续新增：

```http
POST /api/esp-ai-terminal/chat
WS   /ws/esp-ai-terminal/tts
```

鉴权规则：

1. `/health`、`/version`、`/audit/runtime` 可以不鉴权，但不能泄露密钥。
2. `/api/esp-ai-terminal/devices/*` 必须支持 `X-Device-Token` 鉴权。
3. ASR / LLM / TTS 设备侧接口必须校验设备身份。
4. 日志不能打印 token 原文。
5. `DEVICE_SHARED_SECRET` 是 S0.4 临时共享密钥方案，后续可升级为设备证书、JWT 或短期 token。

---

## 11. 火山引擎与模型接入规范

采用模块化云端 API 代理方案：

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

当前不采用：

1. Doubao-实时语音交互。
2. ESP32 直连火山引擎 API。
3. 火山方舟低延迟部署。
4. TPM 保障包。
5. 自定义模型部署。
6. 声音复刻。
7. 服务器本地跑 ASR / LLM / TTS 模型。

### 11.1 ASR 已验证配置

```env
ASR_PROVIDER=volcengine
ASR_WS_URL=wss://openspeech.bytedance.com/api/v3/sauc/bigmodel_async
ASR_APP_KEY=<APP ID>
ASR_ACCESS_KEY=<Access Token>
ASR_RESOURCE_ID=volc.bigasr.sauc.duration
ASR_AUDIO_FORMAT=pcm
ASR_SAMPLE_RATE=16000
ASR_BITS=16
ASR_CHANNELS=1
ASR_PACKET_MS=200
```

重要结论：

1. 当前使用 `ASR_APP_KEY + ASR_ACCESS_KEY`。
2. 不再使用单字段 `ASR_API_KEY` 模式。
3. 正确资源 ID 为 `volc.bigasr.sauc.duration`。
4. `volc.seedasr.sauc.duration` 会返回 `requested resource not granted`。
5. ASR 按上传给服务端处理的音频时长计费，静音只要被上传也会计入时长。

### 11.2 LLM 规划

```env
LLM_PROVIDER=volcengine_ark
ARK_API_KEY=<火山方舟 API Key>
ARK_BASE_URL=https://ark.cn-beijing.volces.com/api/v3
LLM_MODEL=<推理接入点 ID>
```

规则：

1. 先用轻量 / mini / lite 模型跑通链路。
2. 不在服务器本地运行 LLM。
3. 不在设备端保存方舟 API Key。
4. 屏幕不要求显示中文回复。
5. 中文回复文本可以保留在服务器日志、串口日志或内部状态中。
6. V0.7 屏幕只显示 `Response received` / `Reply ready` / `Error` 等状态。

### 11.3 TTS 规划

```env
TTS_PROVIDER=volcengine
TTS_MODEL=seed-tts-2.0
TTS_APP_KEY=<APP ID 或控制台要求字段>
TTS_ACCESS_KEY=<Access Token 或控制台要求字段>
TTS_VOICE_TYPE=<音色 ID>
```

规则：

1. V0.8 是中文输出能力的核心版本。
2. 设备必须能播放中文回复语音。
3. 不要求设备屏幕显示中文回复。
4. TTS 音频可以短期缓存到 SD 卡，但必须有大小限制。
5. 播放链路不得阻塞 UI task。

---

## 12. 密钥与配置规则

设备端：

```text
app_config.h          可提交，占位配置
app_secrets.h         不提交，真实本地配置
app_secrets.h.example 可提交，字段说明
```

服务器端：

```text
.env.example 可提交
.env         不提交，服务器本地真实配置
```

`.gitignore` 必须包含：

```text
app_secrets.h
.env
*.pem
*.key
*.crt
.venv/
__pycache__/
logs/
```

禁止提交：

1. Wi-Fi 密码。
2. 服务器公网 IP / 私密域名。
3. DEVICE_SHARED_SECRET。
4. 火山 ASR_APP_KEY。
5. 火山 ASR_ACCESS_KEY。
6. 火山 ARK_API_KEY。
7. 火山 TTS 相关 Key。
8. 私钥文件。
9. 数据库密码。

---

## 13. 中文注释规则

新增或修改代码时必须尽可能写完整中文注释。

要求：

1. 模块头文件写中文说明。
2. 关键函数写中文函数注释。
3. 状态机分支写中文解释。
4. 硬件相关逻辑必须写清楚硬件原因。
5. 云端接口、鉴权、WebSocket 状态必须写中文说明。
6. 不要写无意义注释，例如“定义变量”“调用函数”。
7. 注释要解释“为什么这样做”。
8. ASR / LLM / TTS 费用控制、超时、最大时长限制必须写清楚原因。
9. 所有密钥脱敏逻辑必须写中文注释说明边界。

示例：

```cpp
/**
 * @brief 请求停止 ASR 上传
 *
 * 用户点击 Stop ASR 后，业务层进入正常收尾流程。
 * 此时如果服务器主动关闭 WebSocket，底层 websocket_client 可能打印 read error，
 * 但这属于正常 FIN 关闭，不应把业务状态误判为 ERROR。
 */
```

---

## 14. 每轮任务执行流程

每次开始任务前：

1. 阅读 `PROJECT_SPEC.md`。
2. 阅读 `AGENTS.md`。
3. 明确当前版本号。
4. 明确本轮允许做什么。
5. 明确本轮禁止做什么。
6. 先说明计划，再修改。

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

服务器端验证：

```bash
python -m compileall app
curl http://127.0.0.1:18080/health
```

---

## 15. 最终提醒

1. `AGENTS.md` 只写规范、边界、职责和安全要求。
2. `PROJECT_SPEC.md` 写版本计划、详细步骤、验收标准和给 Codex 的阶段提示词。
3. 不要跳版本。
4. 服务器只做轻量代理，不跑模型。
5. 后续 ASR / LLM / TTS 都经服务器代理。
6. 不要提交真实密钥。
7. 不要求屏幕显示中文。
8. 最终中文输出由 V0.8 TTS 负责。
