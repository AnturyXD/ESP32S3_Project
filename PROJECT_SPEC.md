# ESP32-S3 触屏 AI 语音终端项目方案（SD 卡 + 云端网关版）

> 适用对象：Codex / AI 编程代理 / 后续开发者  
> 设备端：Waveshare ESP32-S3-Touch-LCD-3.49  
> 开发环境：VS Code + ESP-IDF  
> UI 框架：LVGL  
> 服务器：2 核 4G 内存 6M 带宽 Linux 服务器（已有其他项目运行）  
> 当前目标：在不破坏现有设备功能和服务器已有项目的前提下，继续推进 SD 卡外部存储、服务端代理、云端 ASR/LLM/TTS 链路。

---

## 0. 当前项目状态基线

当前项目已经具备以下能力：

1. V0.1：工程骨架、模块组件、基础日志已完成。
2. V0.2：LCD / Touch / LVGL / Home 页面已完成。
3. V0.3：App Shell、页面路由、独立页面骨架已完成。
4. V0.3.1：返回按钮与页面切换花屏问题已基本修复。
5. V0.4：Wi-Fi STA、IP 获取、SNTP 时间同步、Home/Settings/Debug 状态显示已完成。
6. V0.4.1 / V0.4.2：UI 状态刷新链路已修复，时间/Wi-Fi 状态已能主动刷新。
7. V0.4.5：PWR 电池供电控制已通过实机验收。
8. V0.5：音频 I/O 基础验证已接入，播放测试音已实机通过。
9. 当前发现风险：V0.5 中音频通路 EXIO7 操作可能破坏 EXIO6 / SYS_EN 电池保持供电，需要先做 TCA9554 共享 IO 修复。

---

## 1. 新增总体要求

本阶段开始，项目新增三条硬性要求：

### 1.1 增加 SD 卡作为设备外部存储

设备端应使用 SD 卡作为外部存储，减少内部 Flash、NVS、RAM、PSRAM 压力。

SD 卡主要用于：

1. 运行日志落盘。
2. 音频临时缓存。
3. ASR 上传缓存。
4. 错误快照。
5. 配置备份。
6. 后续 OTA 包暂存。
7. 后续语音对话记录、调试数据、离线资源。

内部 Flash 只保留：

1. 固件本体。
2. NVS 小型配置。
3. 必要证书或设备 ID。
4. 极小量状态数据。

禁止把大块音频、长期日志、调试 dump、TTS 缓存写入内部 Flash。

### 1.2 开始搭建轻量服务器网关

从现在开始，项目不再只规划“设备直连第三方 API”。需要同时建立一套轻量服务器网关，用于后续保护密钥、管理设备、代理 ASR / LLM / TTS。

服务器不是用来本地跑大模型，也不是用来本地跑 ASR/TTS 模型。2 核 4G 6M 的服务器只作为轻量 API 代理和设备管理服务。

服务器职责：

1. 保护豆包 / 火山引擎等云服务 API Key。
2. 给设备下发临时 Token 或设备配置。
3. 代理 ASR WebSocket。
4. 代理 LLM 请求。
5. 代理 TTS WebSocket。
6. 管理设备心跳。
7. 记录设备错误日志。
8. 管理短上下文。
9. 后续支持 OTA 元数据。

### 1.3 以后新代码尽可能写完整中文注释

从本版本起，Codex 生成或修改代码时，必须尽可能写清楚中文注释。

要求：

1. 模块头文件要有中文说明。
2. 关键函数要有中文函数注释。
3. 状态机分支要有中文解释。
4. 硬件引脚、EXIO、I2C、I2S、SD 卡挂载点要有中文注释。
5. 云端 API、鉴权、WebSocket 状态要有中文注释。
6. 不能用无意义注释堆砌，例如“定义变量”“执行函数”。
7. 注释应解释“为什么这样做”，而不只是重复代码。

---

## 2. 总体产品架构

项目拆成两条主线：

```text
设备端 ESP32-S3
  ├── 触屏 UI / LVGL
  ├── App Shell 页面框架
  ├── Wi-Fi / Time / Power / Audio / Storage
  ├── SD 卡外部存储
  ├── ASR/LLM/TTS 客户端
  └── 设备协议客户端

服务器端 esp32-ai-gateway
  ├── 设备注册与心跳
  ├── 设备配置下发
  ├── ASR WebSocket 代理
  ├── LLM 请求代理
  ├── TTS WebSocket 代理
  ├── 日志接收
  ├── Token / API Key 管理
  └── 后续 OTA 元数据
```

---

## 3. 设备端模块结构

设备端组件应保持如下结构：

```text
components/
├── app_config
├── app_shell
├── bsp_board
├── bsp_io_expander        # 推荐新增：统一管理 TCA9554 EXIO6/EXIO7 等共享 IO
├── service_power
├── service_network
├── service_time
├── service_audio
├── service_storage        # 新增：SD 卡与文件系统管理
├── service_ai
├── service_cloud          # 新增：设备与自建服务器通信
├── service_log
└── ui
```

### 3.1 `bsp_io_expander`

用途：统一管理 TCA9554 扩展 IO，避免不同模块分别写同一颗扩展 IO 导致互相覆盖。

必须管理：

1. EXIO6：SYS_EN，电池供电保持。
2. EXIO7：扬声器 / 音频通路使能。
3. 后续可能新增的其他 EXIO 引脚。

必须提供：

```cpp
bsp_io_expander_init();
bsp_io_expander_set_output(uint8_t pin, bool level);
bsp_io_expander_set_direction(uint8_t pin, bool output);
bsp_io_expander_get_output_shadow();
bsp_io_expander_get_direction_shadow();
```

要求：

1. 维护 `output_shadow`。
2. 维护 `direction_shadow`。
3. 所有写操作必须读改写或基于 shadow 修改单 bit。
4. 所有 I2C 写操作必须加 mutex。
5. `service_power` 不允许直接写 TCA9554 寄存器。
6. `service_audio` 不允许直接写 TCA9554 寄存器。

### 3.2 `service_storage`

新增 SD 卡与文件系统服务。

职责：

1. 初始化 SD 卡。
2. 挂载 FATFS 到 `/sdcard`。
3. 创建标准目录结构。
4. 提供文件读写接口。
5. 提供剩余空间查询。
6. 提供日志轮转。
7. 提供音频缓存文件管理。
8. SD 卡缺失时降级运行，不导致系统崩溃。

建议接口：

```cpp
service_storage_init();
service_storage_is_mounted();
service_storage_get_state();
service_storage_get_state_string();
service_storage_get_total_kb();
service_storage_get_free_kb();
service_storage_ensure_dirs();
service_storage_write_text_file(const char *path, const char *text);
service_storage_append_log(const char *tag, const char *line);
service_storage_open_audio_cache_file(...);
service_storage_cleanup_temp_files();
```

建议状态：

```text
STORAGE_STATE_UNINIT
STORAGE_STATE_MOUNTING
STORAGE_STATE_MOUNTED
STORAGE_STATE_NO_CARD
STORAGE_STATE_FORMAT_REQUIRED
STORAGE_STATE_ERROR
```

### 3.3 SD 卡目录规范

挂载点：

```text
/sdcard
```

目录结构：

```text
/sdcard/
├── logs/
│   ├── system_YYYYMMDD.log
│   ├── audio_YYYYMMDD.log
│   └── cloud_YYYYMMDD.log
├── audio/
│   ├── record_cache/
│   ├── asr_upload/
│   ├── tts_cache/
│   └── debug/
├── config/
│   └── device_config_backup.json
├── ota/
│   └── packages/
├── crash/
│   └── panic_*.txt
└── tmp/
```

### 3.4 存储策略

1. 音频 PCM 数据默认不长期保存。
2. 调试模式下才允许保存录音片段。
3. ASR 上传失败时，可将短音频片段暂存在 `/sdcard/audio/asr_upload/`。
4. TTS 音频可短期缓存到 `/sdcard/audio/tts_cache/`，但必须有大小限制。
5. 日志每天轮转，单文件建议限制 512KB ~ 2MB。
6. SD 卡剩余空间低于阈值时自动清理 `tmp` 和旧日志。
7. SD 卡缺失时，设备必须仍能启动，只是禁用外部存储功能。
8. 禁止频繁向内部 Flash 写大数据。

---

## 4. 服务器端总体方案

服务器项目名：

```text
esp32-ai-gateway
```

服务器定位：

```text
轻量 API 网关 + 设备管理 + 云服务代理
```

不承担：

1. 本地大模型推理。
2. 本地 ASR 模型推理。
3. 本地 TTS 模型推理。
4. 大规模数据库业务。
5. 高并发公网服务。

### 4.1 技术选型

推荐默认方案：

```text
Python 3.11+
FastAPI
Uvicorn
WebSocket
SQLite（早期可选）
systemd 部署
Nginx 反向代理（仅在确认不影响现有项目后再接入）
```

原因：

1. FastAPI 支持 HTTP 与 WebSocket，适合 ASR/TTS 代理。
2. Python 便于快速接入第三方 API。
3. systemd 部署对现有服务器侵入较小。
4. 2 核 4G 服务器足够运行轻量代理服务。
5. 不在服务器上跑模型，避免资源不足。

如果服务器已有 Docker 统一运维，也可以改为 Docker Compose；但必须先审计现有 Docker 项目，不能覆盖现有网络、容器名、端口和 volume。

---

## 5. 服务器避让策略

服务器已有其他项目在运行，因此 Codex 在服务器上做任何部署前，必须先做只读审计。

### 5.1 只读审计命令

Codex 连接服务器后，第一步只能运行只读命令：

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

审计完成后，必须生成：

```text
SERVER_AUDIT.md
```

内容包括：

1. 当前开放端口。
2. 当前运行服务。
3. 当前 Docker 容器。
4. 是否安装 Nginx。
5. 是否已有 80/443 站点。
6. 可用磁盘空间。
7. 可用内存。
8. 建议使用端口。
9. 风险点。
10. 下一步部署计划。

### 5.2 严禁行为

Codex 禁止：

1. 未审计就安装服务。
2. 未确认就修改 Nginx 主配置。
3. 未确认就重启 Nginx。
4. 未确认就占用 80 / 443。
5. 未确认就修改防火墙。
6. 未确认就停止或删除现有 Docker 容器。
7. 未确认就执行 `apt upgrade`。
8. 未确认就改 `/var/www` 现有项目。
9. 未确认就使用已有项目的数据库。
10. 未确认就清理日志或删除文件。

### 5.3 推荐部署位置

服务器端项目目录：

```text
/opt/esp32-ai-gateway
```

运行数据：

```text
/var/lib/esp32-ai-gateway
```

日志目录：

```text
/var/log/esp32-ai-gateway
```

密钥配置：

```text
/etc/esp32-ai-gateway/gateway.env
```

运行用户：

```text
esp32ai
```

服务名：

```text
esp32-ai-gateway.service
```

### 5.4 端口避让

默认监听：

```text
127.0.0.1:18080
```

说明：

1. 默认只绑定本机回环地址，避免直接暴露公网。
2. 如果需要公网访问，优先通过 Nginx 反向代理。
3. 如果 18080 被占用，改用 18081 / 18082。
4. 不直接占用 80 / 443。
5. 不占用现有项目端口。

### 5.5 Nginx 接入策略

只有在确认服务器已有 Nginx 且允许新增路由时，才添加独立配置文件：

```text
/etc/nginx/conf.d/esp32-ai-gateway.conf
```

推荐路径前缀：

```text
/esp32-ai/
```

WebSocket 路径：

```text
/esp32-ai/ws/asr
/esp32-ai/ws/tts
```

修改 Nginx 前必须：

1. 备份相关配置。
2. 使用 `nginx -t` 测试配置。
3. 只 reload，不 restart。
4. reload 前汇报风险。

---

## 6. 服务器 API 设计

### 6.1 健康检查

```http
GET /health
```

返回：

```json
{
  "ok": true,
  "service": "esp32-ai-gateway",
  "version": "0.1.0"
}
```

### 6.2 设备注册

```http
POST /api/v1/device/register
```

请求：

```json
{
  "device_id": "esp32s3-xxxx",
  "firmware_version": "0.6.0",
  "hardware": "ESP32-S3-Touch-LCD-3.49"
}
```

返回：

```json
{
  "ok": true,
  "device_token": "temporary-token",
  "server_time": 1710000000
}
```

### 6.3 心跳

```http
POST /api/v1/device/heartbeat
```

请求：

```json
{
  "device_id": "esp32s3-xxxx",
  "firmware_version": "0.6.0",
  "wifi_rssi": -55,
  "free_heap": 123456,
  "sd_mounted": true,
  "audio_state": "READY"
}
```

### 6.4 配置下发

```http
GET /api/v1/device/config?device_id=esp32s3-xxxx
```

返回：

```json
{
  "ok": true,
  "asr_enabled": true,
  "llm_enabled": true,
  "tts_enabled": true,
  "audio_sample_rate": 16000,
  "audio_channels": 1,
  "log_level": "INFO"
}
```

### 6.5 ASR WebSocket 代理

```text
WS /ws/asr?device_id=esp32s3-xxxx&token=xxx
```

设备发送：

1. `start` JSON 消息。
2. PCM binary frame。
3. `stop` JSON 消息。

服务器返回：

1. partial text。
2. final text。
3. error。

### 6.6 LLM 代理

```http
POST /api/v1/chat
```

### 6.7 TTS WebSocket 代理

```text
WS /ws/tts?device_id=esp32s3-xxxx&token=xxx
```

---

## 7. 服务器资源限制

2 核 4G 6M 服务器只适合轻量代理。Codex 必须遵守：

1. 不在服务器上运行大模型。
2. 不在服务器上运行本地 ASR。
3. 不在服务器上运行本地 TTS。
4. 单设备音频流优先。
5. 默认并发设备数限制为 1~3。
6. ASR/TTS 音频流必须设置超时。
7. 单连接最大时长限制，例如 60~120 秒。
8. 日志必须轮转。
9. 不保存大量音频，除非调试开关开启。
10. 服务器日志不能打印完整 API Key。

---

## 8. 配置与密钥规范

设备端：

```text
app_config.h                 # 可提交，占位配置
app_secrets.h                # 不提交，本地真实 Wi-Fi / device token
app_secrets.h.example        # 可提交，字段示例
```

服务器端：

```text
.env.example                 # 可提交
.env                         # 不提交
/etc/esp32-ai-gateway/gateway.env  # 服务器真实密钥，不进仓库
```

必须隐藏：

1. Wi-Fi Password。
2. 豆包 / 火山 API Key。
3. device_token。
4. server admin token。
5. 数据库密码。

---

## 9. 更新后的版本路线

### V0.5.2：TCA9554 共享 IO 状态保护修复

目标：修复 EXIO7 音频使能覆盖 EXIO6 电源保持的问题。

必须完成：

1. 新增或完善 `bsp_io_expander`。
2. 统一 TCA9554 访问。
3. EXIO6 / EXIO7 可同时保持 HIGH。
4. service_power 和 service_audio 不再各自直接写 TCA9554。
5. 电池供电开机、测试音播放、长按关机全部正常。

### V0.5.3：SD 卡外部存储接入

目标：引入 SD 卡作为外部存储，减轻内部 Flash / RAM 压力。

必须完成：

1. 新增 `service_storage`。
2. 挂载 SD 卡到 `/sdcard`。
3. 创建标准目录。
4. Debug 页面显示 SD 状态、总容量、剩余容量。
5. service_log 支持日志写入 SD 卡。
6. 音频调试数据可选写入 SD 卡。
7. SD 缺失时系统正常启动。

### V0.5.4：配置安全与中文注释规范

目标：清理真实配置，建立中文注释规范。

必须完成：

1. 移除真实 Wi-Fi / API Key。
2. 增加 `app_secrets.h.example`。
3. 增加 `.gitignore`。
4. 关键代码补充中文注释。
5. README 增加本地配置说明。

### S0.1：服务器只读审计与部署计划

目标：审计已有服务器环境，不做破坏性修改。

必须完成：

1. 执行只读审计命令。
2. 生成 `SERVER_AUDIT.md`。
3. 确认端口避让方案。
4. 确认部署方式：systemd 或 Docker Compose。
5. 不启动正式服务。

### S0.2：服务器最小骨架

目标：搭建 `esp32-ai-gateway` 最小服务。

必须完成：

1. FastAPI 项目结构。
2. `/health`。
3. 配置读取。
4. 日志目录。
5. systemd service 或 docker-compose。
6. 默认绑定 `127.0.0.1:18080`。
7. 不影响现有项目。

### S0.3：设备注册与心跳

目标：设备可以连服务器注册和上报状态。

必须完成：

1. 服务器实现 `/api/v1/device/register`。
2. 服务器实现 `/api/v1/device/heartbeat`。
3. 设备端新增 `service_cloud`。
4. 设备端可以发送心跳。
5. Debug 页面显示云连接状态。

### V0.6：ASR 接入（经自建服务器代理）

目标：设备麦克风 PCM → 服务器 ASR WebSocket 代理 → 云端 ASR → 返回识别文字。

必须完成：

1. 设备端不直接保存云端 API Key。
2. 服务器端保存云端 ASR Key。
3. 设备端通过 `service_cloud` / `service_ai` 连接服务器 ASR WS。
4. AI Voice 页面显示识别文本。

### V0.7：LLM 文本对话代理

目标：ASR 文本 → 服务器 LLM 代理 → 豆包回复文本。

### V0.8：TTS 代理与播放

目标：LLM 文本 → 服务器 TTS 代理 → 设备播放音频。

### V0.9：AI 状态机稳定性

目标：统一 ASR / LLM / TTS 状态机、错误恢复、超时、取消。

### V1.0：MVP 演示版

目标：完整闭环可演示。

---

## 10. Codex 执行原则

1. 每次只做一个版本。
2. 先修复 V0.5.2，再做 SD 卡，再做服务器。
3. 不允许同时改设备端和服务器端的多个复杂功能。
4. 每次修改后必须 build。
5. 服务器端每次修改后必须至少能运行 `/health`。
6. 所有新增代码尽可能写中文注释。
7. 不提交真实密钥。
8. 不破坏已有服务器项目。
9. 不在服务器上运行大模型。
10. 不把大文件写入 ESP32 内部 Flash。

---

## 11. 下一轮建议给 Codex 的任务顺序

### 任务 1：V0.5.2 TCA9554 修复

先修复 EXIO6 / EXIO7 共享状态，保证 PWR 与音频同时正常。

### 任务 2：V0.5.3 SD 卡存储

接入 service_storage，建立 `/sdcard` 目录规范。

### 任务 3：S0.1 服务器审计

只读审计服务器，不部署，不改现有服务。

### 任务 4：S0.2 服务器最小骨架

搭建 FastAPI + `/health` + systemd / Docker Compose。

### 任务 5：S0.3 设备心跳

设备端新增 service_cloud，上报设备状态。

### 任务 6：V0.6 ASR 代理

通过自建服务器转发 ASR。

---

## 12. 重要验收标准

### 设备端验收

```text
[ ] PWR 电池供电保持正常
[ ] 测试音仍然有声音
[ ] 录音 Peak 正常变化
[ ] SD 卡挂载成功
[ ] SD 缺失时系统不崩溃
[ ] 日志可写入 SD 卡
[ ] Home / Settings / Debug 页面正常
[ ] Wi-Fi / Time 不受影响
[ ] app_main.cpp 仍然简洁
```

### 服务器端验收

```text
[ ] 已生成 SERVER_AUDIT.md
[ ] 没有占用现有项目端口
[ ] 没有修改现有项目配置
[ ] 服务默认监听 127.0.0.1:18080
[ ] /health 正常返回
[ ] systemd 或 Docker 配置独立
[ ] 日志写入 /var/log/esp32-ai-gateway
[ ] 密钥放在 /etc/esp32-ai-gateway/gateway.env
[ ] 不在仓库提交真实密钥
```

---

## 13. 中文注释标准

所有后续代码尽量采用中文注释。

推荐格式：

```cpp
/**
 * @brief 初始化 SD 卡外部存储服务
 *
 * 该函数负责挂载 SD 卡、创建标准目录，并更新存储服务状态。
 * 如果 SD 卡不存在，函数不会导致系统崩溃，而是进入 NO_CARD 状态，
 * 设备仍然可以继续运行，只是禁用日志落盘和音频缓存功能。
 */
esp_err_t service_storage_init(void);
```

不推荐：

```cpp
// 初始化
// 设置变量
// 调用函数
```

注释要解释目的、边界、风险和硬件原因。

---

## 14. 参考原则

1. SD 卡适合日志、音频缓存、临时文件和外部资源。
2. 内部 Flash 写入要克制，避免大容量和高频写入。
3. TCA9554 是共享 IO 资源，必须统一管理。
4. 服务器只做轻量代理，不做模型推理。
5. 服务器已有其他项目，任何部署都必须先审计和避让。
6. 设备端不长期保存云端 API Key。
7. 后续 ASR / LLM / TTS 都优先经自建服务器代理。
