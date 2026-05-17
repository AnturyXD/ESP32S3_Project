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
V0.5.2  TCA9554 共享 IO 状态保护修复
V0.5.3  SD 卡外部存储接入
V0.5.4  配置安全与中文注释规范
S0.1    服务器只读审计与部署计划
S0.2    服务器最小骨架 /health
S0.3    设备注册与心跳
V0.6    ASR 接入，经自建服务器代理
V0.7    LLM 文本对话代理
V0.8    TTS 代理与播放
V0.9    AI 状态机稳定性
V1.0    MVP 演示版
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
