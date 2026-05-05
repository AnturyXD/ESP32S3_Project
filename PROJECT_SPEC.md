# ESP32-S3 触屏 AI 语音终端项目开发文档

> 目标读者：Codex / AI 编程代理 / 后续开发者  
> 开发环境：VS Code + ESP-IDF  
> UI 框架：LVGL  
> 硬件平台：Waveshare ESP32-S3-Touch-LCD-3.49  
> 项目目标：开发一款基于 ESP32-S3 触摸屏开发板的模块化 AI 语音交互终端。

---

## 0. 重要开发原则

本项目不是普通 Demo，而是一个具备扩展能力的嵌入式产品原型。开发时必须遵守以下原则：

1. 不要把所有逻辑写在 `main.cpp` 中。
2. 不要让 UI 页面直接调用网络 API。
3. 不要让 AI 接口、音频采集、页面显示互相强耦合。
4. 所有功能必须模块化，后续可以继续添加新的 App 页面。
5. 每个版本必须能独立编译、烧录、运行。
6. 每完成一个阶段，必须保留清晰的串口日志。
7. MVP 阶段允许把 Wi-Fi 和 API Key 写在配置文件中，但必须明确注释：这只适合开发测试，正式产品必须改为服务器代理或临时 Token。
8. 不要一次性完成所有功能。必须按版本路线逐步实现。
9. 每一步修改后必须尽量执行构建检查，例如 `idf.py build`。
10. 如果当前硬件驱动、官方示例或库版本存在不确定性，应优先跑通 Waveshare 官方 Demo，再抽象成项目组件。

---

## 1. 项目背景

本项目使用 Waveshare ESP32-S3-Touch-LCD-3.49 作为硬件平台。设备具备触摸屏、Wi-Fi、音频输入输出、麦克风阵列、扬声器接口、RTC、IMU、TF 卡等资源，适合开发一个小型桌面 AI 语音交互终端。

产品的核心体验是：

1. 用户通过触摸屏进入不同功能页面。
2. 首页显示时间、网络状态、设备状态和功能入口。
3. 用户进入 AI 语音助手页面后，可以通过语音和豆包大模型进行交流。
4. 设备通过串口打印完整调试信息，方便开发调试。
5. 整个程序架构必须方便后续增加新的 App 功能。

---

## 2. 明确边界与可行性判断

### 2.1 可以本地实现的能力

以下能力可以在 ESP32-S3 本地实现：

- LVGL 图形界面。
- 触摸交互。
- 页面路由。
- App 入口管理。
- Wi-Fi 连接。
- SNTP 时间同步。
- 串口日志。
- 麦克风采集。
- 扬声器播放。
- 本地状态机。
- 本地配置读取。
- 简单缓存和调试页面。

### 2.2 必须依赖云端的能力

以下能力不能在 ESP32-S3 本地完整实现，必须依赖云端服务：

- 豆包大模型文本对话。
- 通用自然语言理解。
- 高质量语音识别 ASR。
- 高质量语音合成 TTS。
- 类似手机豆包电话的连续语音对话体验。

原因：ESP32-S3 的算力和内存不足以本地运行豆包这类通用大模型。设备端应只负责采集音频、显示界面、维护状态机和播放音频。

### 2.3 是否需要服务器

开发测试阶段可以不做服务器，直接在固件配置中填写：

- Wi-Fi SSID。
- Wi-Fi Password。
- Doubao / Volcengine API Key。
- ASR 模型名或资源 ID。
- LLM 模型 ID。
- TTS 模型名和音色 ID。

正式产品或长期演示版本建议增加服务器。服务器用于：

- 保护 API Key。
- 给设备下发临时 Token。
- 代理 ASR / LLM / TTS 请求。
- 维护多轮上下文。
- 控制调用频率和成本。
- 记录错误日志。
- 后续支持 OTA、账号系统、设备管理。

---

## 3. 产品功能范围

### 3.1 MVP 必须包含

1. 主页面 Home。
2. AI 语音助手页面 AI Voice。
3. 设置页面 Settings。
4. 调试页面 Debug。
5. 关于页面 About。
6. 页面路由和返回逻辑。
7. Wi-Fi 连接。
8. 时间显示。
9. 串口日志。
10. 音频录制与播放测试。
11. AI 语音对话基础链路：ASR → LLM → TTS。

### 3.2 MVP 暂不强制实现

以下功能暂不作为第一版强制目标：

- 唤醒词。
- 全双工实时通话。
- AI 回复时用户打断。
- 本地离线大模型。
- 屏幕配网。
- OTA。
- 用户账号。
- 云端知识库。
- 复杂动画。
- 长上下文记忆。

### 3.3 后续扩展方向

后续可以扩展：

- 天气 App。
- 备忘录 App。
- 智能家居控制 App。
- TF 卡音乐播放器。
- 设备状态监控 App。
- OTA 升级 App。
- 服务端代理。
- 实时语音大模型 Provider。

---

## 4. 推荐技术架构

整体架构如下：

```text
LVGL UI 层
  ↓
App Shell 应用框架层
  ↓
业务服务层
  ↓
BSP 硬件抽象层
  ↓
ESP-IDF / FreeRTOS / Drivers
```

### 4.1 分层说明

#### 4.1.1 BSP 硬件抽象层

负责屏幕、触摸、音频、按键、背光、RTC、IMU 等硬件初始化。

建议组件名：

```text
components/bsp_board
```

#### 4.1.2 UI 层

负责 LVGL 初始化、主题、控件、页面、状态栏和页面切换。

建议组件名：

```text
components/ui
```

#### 4.1.3 App Shell 层

负责应用注册、页面生命周期、页面路由和统一事件分发。

建议组件名：

```text
components/app_shell
```

#### 4.1.4 服务层

负责网络、时间、音频、AI、日志、配置等业务能力。

建议组件：

```text
components/service_network
components/service_time
components/service_audio
components/service_ai
components/service_log
components/app_config
```

---

## 5. 推荐工程目录

Codex 应按照以下结构创建工程：

```text
project_root
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
├── README.md
├── AGENTS.md
├── PROJECT_SPEC.md
│
├── main
│   ├── CMakeLists.txt
│   └── app_main.cpp
│
├── components
│   ├── app_config
│   │   ├── include
│   │   │   └── app_config.h
│   │   ├── app_config.cpp
│   │   └── CMakeLists.txt
│   │
│   ├── bsp_board
│   │   ├── include
│   │   │   └── bsp_board.h
│   │   ├── bsp_board.cpp
│   │   ├── bsp_lcd.cpp
│   │   ├── bsp_touch.cpp
│   │   ├── bsp_audio.cpp
│   │   ├── bsp_backlight.cpp
│   │   └── CMakeLists.txt
│   │
│   ├── app_shell
│   │   ├── include
│   │   │   ├── app_shell.h
│   │   │   ├── app_router.h
│   │   │   └── app_events.h
│   │   ├── app_shell.cpp
│   │   ├── app_router.cpp
│   │   ├── app_events.cpp
│   │   └── CMakeLists.txt
│   │
│   ├── ui
│   │   ├── include
│   │   │   ├── ui.h
│   │   │   ├── ui_router.h
│   │   │   └── ui_pages.h
│   │   ├── ui_init.cpp
│   │   ├── ui_theme.cpp
│   │   ├── ui_status_bar.cpp
│   │   ├── ui_router.cpp
│   │   ├── pages
│   │   │   ├── ui_home.cpp
│   │   │   ├── ui_ai_voice.cpp
│   │   │   ├── ui_settings.cpp
│   │   │   ├── ui_debug.cpp
│   │   │   └── ui_about.cpp
│   │   └── CMakeLists.txt
│   │
│   ├── service_network
│   ├── service_time
│   ├── service_audio
│   ├── service_ai
│   └── service_log
│
└── docs
    ├── VERSION_ROADMAP.md
    ├── AI_PIPELINE.md
    ├── UI_SPEC.md
    └── DEBUG_GUIDE.md
```

---

## 6. 页面设计要求

### 6.1 全局 UI 要求

屏幕为窄长屏，UI 必须采用竖向布局。不要设计复杂横向表格。页面需要大按钮、大间距、清晰状态提示。

全局状态栏应包含：

- 当前时间。
- Wi-Fi 状态。
- 设备状态。
- 返回按钮或页面标题。

### 6.2 Home 页面

Home 页面用于展示设备基础信息和 App 入口。

必须包含：

- 当前时间。
- 日期。
- Wi-Fi 状态。
- AI 状态摘要。
- App 卡片入口。

App 入口：

```text
AI Voice
Settings
Debug
About
Placeholder 1
Placeholder 2
```

### 6.3 AI Voice 页面

AI Voice 页面用于完成语音对话。

必须包含：

- 当前 AI 状态。
- 开始录音按钮。
- 停止录音按钮。
- 停止播放按钮。
- 用户识别文本区域。
- AI 回复文本区域。
- 错误提示区域。
- 返回主页按钮。

AI 页面状态：

```text
IDLE
LISTENING
RECOGNIZING
THINKING
SPEAKING
ERROR
```

### 6.4 Settings 页面

MVP 阶段 Settings 页面以只读显示为主。

显示内容：

- Wi-Fi SSID。
- Wi-Fi 状态。
- 当前 IP。
- AI Provider。
- ASR 模型。
- LLM 模型。
- TTS 模型。
- TTS 音色。
- 音量。
- 屏幕亮度。

### 6.5 Debug 页面

Debug 页面用于显示设备运行摘要，不替代串口日志。

显示内容：

- 最近 10 条日志。
- 当前页面。
- 当前 AI 状态。
- 当前网络状态。
- 可用堆内存。
- 可用 PSRAM。
- 最近一次错误。

### 6.6 About 页面

显示内容：

- 产品名称。
- 固件版本。
- 编译时间。
- ESP-IDF 版本。
- LVGL 版本。
- 开发板型号。
- 作者信息。

---

## 7. AI 语音链路设计

### 7.1 MVP 链路

MVP 阶段采用三段式语音链路：

```text
用户语音
  ↓
麦克风采集 PCM
  ↓
ASR 语音识别
  ↓
用户文字
  ↓
LLM 文本对话
  ↓
AI 回复文字
  ↓
TTS 语音合成
  ↓
扬声器播放
```

### 7.2 不要一开始做全双工

第一阶段不要实现类似电话的全双工对话。应先实现：

1. 点击开始录音。
2. 点击结束录音。
3. 上传音频识别。
4. 获得文本。
5. 请求大模型。
6. 获得回复。
7. 请求 TTS。
8. 播放音频。

### 7.3 AI 服务抽象

AI 服务必须抽象为 Provider，不能写死单个 API。

建议结构：

```text
service_ai
├── ai_conversation_manager
├── ai_state_machine
├── ai_provider_base
├── ai_provider_doubao_pipeline
├── ai_asr_client
├── ai_llm_client
├── ai_tts_client
├── ai_context
└── ai_error
```

### 7.4 AI 状态机

```text
IDLE
  ↓ user_start_record
LISTENING
  ↓ user_stop_record
RECOGNIZING
  ↓ asr_success
THINKING
  ↓ llm_success
TTS_PROCESSING
  ↓ tts_audio_ready
SPEAKING
  ↓ playback_done
IDLE
```

任意状态遇到错误：

```text
ANY_STATE
  ↓ error
ERROR
  ↓ retry / back
IDLE
```

---

## 8. 事件系统设计

项目应采用事件驱动，避免模块互相直接调用。

### 8.1 系统事件

```text
APP_EVT_BOOT_START
APP_EVT_BOOT_DONE
APP_EVT_WIFI_CONNECTING
APP_EVT_WIFI_CONNECTED
APP_EVT_WIFI_DISCONNECTED
APP_EVT_TIME_SYNCED
APP_EVT_PAGE_CHANGED
APP_EVT_ERROR
```

### 8.2 AI 事件

```text
AI_EVT_START_RECORD
AI_EVT_STOP_RECORD
AI_EVT_ASR_STARTED
AI_EVT_ASR_PARTIAL_TEXT
AI_EVT_ASR_FINAL_TEXT
AI_EVT_LLM_STARTED
AI_EVT_LLM_TEXT_DELTA
AI_EVT_LLM_DONE
AI_EVT_TTS_STARTED
AI_EVT_TTS_AUDIO_DELTA
AI_EVT_TTS_DONE
AI_EVT_PLAYBACK_STARTED
AI_EVT_PLAYBACK_DONE
AI_EVT_INTERRUPTED
AI_EVT_ERROR
```

### 8.3 UI 更新规则

LVGL 控件更新应集中在 UI 任务中执行。其他任务不能随意直接修改 LVGL 控件。其他任务应通过事件队列通知 UI 层刷新。

---

## 9. FreeRTOS 任务建议

建议任务如下：

| 任务 | 职责 | 优先级建议 |
|---|---|---:|
| UI Task | LVGL tick、页面刷新、触摸事件处理 | 中 |
| Network Task | Wi-Fi 连接、断线重连、网络状态 | 中 |
| Time Task | SNTP 同步、时间广播 | 低 |
| Audio Capture Task | 麦克风采集、PCM 缓冲 | 高 |
| Audio Playback Task | TTS 音频播放 | 高 |
| AI Task | ASR / LLM / TTS 状态机 | 中高 |
| Log Task | 日志缓存、Debug 页面摘要 | 低 |

---

## 10. 配置文件要求

建议创建：

```text
components/app_config/include/app_config.h
```

其中包含：

```text
WIFI_SSID
WIFI_PASSWORD

AI_USE_BACKEND_SERVER
AI_PROVIDER

DOUBAO_API_KEY
DOUBAO_ASR_MODEL
DOUBAO_LLM_MODEL
DOUBAO_TTS_MODEL
DOUBAO_TTS_VOICE

SERVER_BASE_URL
SERVER_DEVICE_ID

AUDIO_SAMPLE_RATE
AUDIO_BITS
AUDIO_CHANNELS

LOG_LEVEL
UI_THEME
```

必须在配置文件顶部写清楚：

```text
WARNING:
Storing API keys in firmware is only acceptable for local development and demo testing.
For production, use a backend server or temporary token mechanism.
Do not commit real API keys to Git.
```

---

## 11. 串口日志要求

必须使用统一日志风格。

日志 Tag 建议：

```text
APP
BSP
LCD
TOUCH
AUDIO_IN
AUDIO_OUT
WIFI
TIME
UI
AI
ASR
LLM
TTS
ERROR
```

必须打印以下日志：

- 系统启动。
- PSRAM 状态。
- LCD 初始化结果。
- 触摸初始化结果。
- LVGL 初始化结果。
- Wi-Fi 开始连接。
- Wi-Fi 连接成功。
- Wi-Fi 断线。
- 时间同步成功。
- 页面切换。
- 录音开始。
- 录音结束。
- ASR 请求开始。
- ASR 返回文本。
- LLM 请求开始。
- LLM 返回文本。
- TTS 请求开始。
- TTS 音频返回。
- 播放开始。
- 播放结束。
- 错误码。

---

## 12. 版本路线

### V0.1：工程基础与日志

目标：建立 ESP-IDF 工程、组件结构、基础日志。

任务：

1. 创建标准 ESP-IDF 工程。
2. 创建组件目录。
3. 创建 `app_config`。
4. 创建 `service_log`。
5. `app_main.cpp` 只负责初始化模块，不写业务逻辑。
6. 串口输出启动日志。

验收标准：

- `idf.py build` 成功。
- 烧录后串口输出启动日志。
- 工程目录结构清晰。
- 没有把所有代码写进 `main.cpp`。

---

### V0.2：屏幕、触摸、LVGL 首页

目标：跑通 LCD、Touch、LVGL，并显示 Home 页面。

任务：

1. 参考 Waveshare 官方示例初始化屏幕。
2. 初始化触摸。
3. 初始化 LVGL。
4. 创建 UI Task。
5. 创建 Home 页面。
6. 显示 App 入口卡片。

验收标准：

- 屏幕正常显示。
- 触摸有响应。
- Home 页面可见。
- 串口输出 LCD、Touch、LVGL 初始化日志。

---

### V0.3：App Shell 与页面路由

目标：完成基础页面框架。

任务：

1. 创建 `app_shell`。
2. 创建页面注册机制。
3. 创建页面切换方法。
4. 完成 Home / AI Voice / Settings / Debug / About 页面空壳。
5. 实现返回 Home。

验收标准：

- 首页点击不同入口能进入对应页面。
- 每个页面都有标题和返回按钮。
- 页面切换有日志。
- 新增页面不需要重写路由核心逻辑。

---

### V0.4：Wi-Fi 与时间同步

目标：设备可以联网并显示时间。

任务：

1. 实现 `service_network`。
2. 从 `app_config` 读取 Wi-Fi SSID 和密码。
3. 连接 Wi-Fi。
4. 实现断线重连。
5. 实现 `service_time`。
6. 首页显示时间和网络状态。
7. Settings 页面显示 IP 和 Wi-Fi 状态。

验收标准：

- Wi-Fi 可以连接。
- 首页显示在线状态。
- 首页时间正确刷新。
- 断网后 UI 显示离线。
- 串口打印网络状态变化。

---

### V0.5：音频输入输出验证

目标：跑通麦克风采集和扬声器播放。

任务：

1. 实现 `service_audio`。
2. 初始化 I2S / Codec。
3. 实现 PCM 录音缓冲。
4. 实现测试音频播放。
5. AI 页面增加录音按钮和播放测试按钮。
6. Debug 页面显示音频状态。

验收标准：

- 点击录音按钮后串口显示采集状态。
- 能获取 PCM 数据长度。
- 能播放测试音频。
- 录音和播放不会导致 UI 卡死。

---

### V0.6：ASR 接入

目标：将用户语音转成文字。

任务：

1. 实现 `ai_asr_client`。
2. 建立 ASR WebSocket 连接。
3. 发送 PCM 音频。
4. 获取识别结果。
5. AI 页面显示用户识别文本。
6. 添加 ASR 错误处理。

验收标准：

- 用户说话后页面能显示识别文本。
- ASR 开始、结束、失败都有日志。
- 网络失败能进入 ERROR 状态。

---

### V0.7：LLM 接入

目标：将识别文本发送给豆包大模型并显示回复。

任务：

1. 实现 `ai_llm_client`。
2. 支持文本请求。
3. 支持流式文本返回则优先使用流式。
4. 管理短上下文。
5. AI 页面显示 AI 回复文本。

验收标准：

- 用户语音转文字后能得到 AI 文本回复。
- 至少支持 3 轮对话。
- LLM 请求失败能显示错误。

---

### V0.8：TTS 接入与语音回复

目标：AI 回复可以通过扬声器播放。

任务：

1. 实现 `ai_tts_client`。
2. 将 AI 回复文本发送给 TTS。
3. 获取 PCM 或可播放音频。
4. 边接收边播放，或先缓存再播放。
5. AI 页面显示播放状态。
6. 支持停止播放。

验收标准：

- AI 回复能被播放出来。
- 播放期间页面显示 SPEAKING。
- 可以停止播放。
- 播放结束后回到 IDLE。

---

### V0.9：稳定性与体验优化

目标：修复卡顿、内存、重连、错误恢复问题。

任务：

1. 加入超时控制。
2. 加入网络重试。
3. 加入音频 Ring Buffer 水位日志。
4. 限制上下文长度。
5. 优化 LVGL 刷新频率。
6. Debug 页面显示最近错误。
7. 统一错误码。

验收标准：

- 连续使用 10 分钟不重启。
- 多次页面切换不崩溃。
- 断网后能恢复。
- 错误原因可通过串口定位。

---

### V1.0：完整 MVP 发布

目标：形成一个可演示、可扩展、可维护的完整版本。

任务：

1. 整理 README。
2. 整理配置说明。
3. 整理烧录说明。
4. 整理已知问题。
5. 整理后续计划。
6. 清理无用代码。
7. 确认所有页面可用。
8. 确认 AI 链路完整可用。

验收标准：

- 新开发者可根据 README 配置、编译、烧录。
- AI Voice 完成“说话 → 识别 → 回复 → 播放”完整闭环。
- 所有模块职责清楚。
- 代码结构支持后续扩展。

---

## 13. Codex 执行方式要求

Codex 必须按以下方式工作：

1. 每次只做一个版本或一个小任务。
2. 修改前先阅读 `PROJECT_SPEC.md`、`AGENTS.md`、`README.md`。
3. 修改前说明计划。
4. 修改后说明改了哪些文件。
5. 修改后尝试执行构建。
6. 构建失败必须先分析原因，再修改。
7. 不允许为了绕过错误删除核心功能。
8. 不允许把 Mock 当成最终功能，除非明确标记为 Mock。
9. 不允许提交真实 API Key。
10. 每完成一个版本，要更新 README 或版本记录。

---

## 14. 建议 AGENTS.md 内容

请在仓库根目录创建 `AGENTS.md`，内容如下：

```markdown
# AGENTS.md

## Role
You are developing an ESP-IDF + LVGL embedded product for Waveshare ESP32-S3-Touch-LCD-3.49.

## Product Goal
Build a modular touchscreen AI voice terminal. It must have a Home page, independent app pages, serial debugging logs, Wi-Fi connectivity, audio input/output, and a future-proof AI voice pipeline.

## Hard Rules
- Do not put all logic in main/app_main.cpp.
- Do not let UI directly call cloud APIs.
- Do not hardcode business logic into page files.
- Use modular ESP-IDF components.
- Keep every milestone buildable.
- Do not commit real Wi-Fi password or API keys.
- Use clear ESP_LOG tags for every module.
- Prefer event-driven design.
- UI updates must be performed safely in the LVGL/UI context.

## Required Components
- app_config
- bsp_board
- ui
- app_shell
- service_network
- service_time
- service_audio
- service_ai
- service_log

## Version Discipline
Work in small milestones:
V0.1 project skeleton and logs
V0.2 LCD + touch + LVGL home
V0.3 app shell and page routing
V0.4 Wi-Fi and time
V0.5 audio input/output
V0.6 ASR
V0.7 LLM
V0.8 TTS playback
V0.9 stability
V1.0 MVP release

## Build Check
After every meaningful change, run:

```bash
idf.py build
```

If build fails, explain the cause before patching.
```

---

## 15. 每轮给 Codex 的推荐提示词

### 15.1 初始化项目提示词

```text
请阅读 PROJECT_SPEC.md 和 AGENTS.md，先不要实现 AI 功能。请创建 ESP-IDF 项目基础结构，按照文档建立 components 目录，包括 app_config、bsp_board、ui、app_shell、service_network、service_time、service_audio、service_ai、service_log。当前目标是 V0.1：工程基础与日志。

要求：
1. app_main.cpp 只做总初始化，不写具体业务。
2. 每个组件要有 CMakeLists.txt。
3. 创建基础日志模块。
4. 创建配置头文件 app_config.h。
5. 不要写真实 Wi-Fi 密码或 API Key。
6. 修改完成后运行 idf.py build，并报告结果。
```

### 15.2 屏幕和 LVGL 提示词

```text
当前进入 V0.2。请基于现有工程实现 LCD、Touch、LVGL 初始化，并显示 Home 页面。

要求：
1. 优先参考 Waveshare ESP32-S3-Touch-LCD-3.49 官方示例的屏幕与触摸初始化方式。
2. 将屏幕和触摸相关代码封装在 bsp_board 中。
3. 将 LVGL 初始化和页面创建放在 ui 组件中。
4. Home 页面显示时间占位、Wi-Fi 状态占位和 App 入口卡片。
5. 不要实现 AI 功能。
6. 串口必须打印 LCD、Touch、LVGL 初始化日志。
7. 修改完成后运行 idf.py build。
```

### 15.3 页面路由提示词

```text
当前进入 V0.3。请实现 App Shell 和页面路由。

要求：
1. 创建 Home、AI Voice、Settings、Debug、About 五个页面。
2. 每个页面独立文件。
3. 首页点击卡片可以进入对应页面。
4. 每个页面都有返回 Home 的按钮。
5. 页面切换必须打印日志。
6. 不能让页面之间互相直接依赖。
7. 修改完成后运行 idf.py build。
```

### 15.4 Wi-Fi 和时间提示词

```text
当前进入 V0.4。请实现 Wi-Fi 连接和时间同步。

要求：
1. Wi-Fi SSID 和密码从 app_config.h 读取。
2. 不要提交真实密码，只保留占位宏。
3. service_network 负责 Wi-Fi 初始化、连接、断线重连和状态通知。
4. service_time 负责 SNTP 时间同步。
5. Home 页面显示当前时间和 Wi-Fi 状态。
6. Settings 页面显示 SSID、IP、网络状态。
7. 修改完成后运行 idf.py build。
```

### 15.5 音频验证提示词

```text
当前进入 V0.5。请实现音频输入输出基础验证。

要求：
1. 在 service_audio 中封装麦克风采集和扬声器播放。
2. 优先参考 Waveshare 官方音频示例。
3. 支持 16kHz、16bit、单声道 PCM 作为默认配置。
4. AI Voice 页面添加录音测试按钮和播放测试按钮。
5. 串口打印采集到的 PCM 长度和播放状态。
6. 不要接入 ASR、LLM、TTS。
7. 修改完成后运行 idf.py build。
```

### 15.6 ASR 提示词

```text
当前进入 V0.6。请实现 ASR 客户端。

要求：
1. 在 service_ai 中新增 ai_asr_client。
2. 使用 WebSocket 方式连接语音识别服务。
3. 音频格式按 16kHz、16bit、单声道 PCM 设计。
4. API Key 和模型参数从 app_config.h 读取。
5. 不要在串口打印完整 API Key。
6. AI Voice 页面显示识别结果。
7. 失败时进入 AI_STATE_ERROR。
8. 修改完成后运行 idf.py build。
```

### 15.7 LLM 提示词

```text
当前进入 V0.7。请实现 LLM 文本对话客户端。

要求：
1. 在 service_ai 中新增 ai_llm_client。
2. ASR 得到的文本应传入 LLM。
3. LLM 回复文本应通过事件发送给 UI。
4. 保留最近 3 轮上下文即可。
5. 不要把上下文无限保存。
6. AI Voice 页面显示用户文本和 AI 回复文本。
7. 修改完成后运行 idf.py build。
```

### 15.8 TTS 提示词

```text
当前进入 V0.8。请实现 TTS 和语音播放。

要求：
1. 在 service_ai 中新增 ai_tts_client。
2. LLM 回复文本应传入 TTS。
3. TTS 返回音频后交给 service_audio 播放。
4. 支持播放中停止。
5. AI Voice 页面显示 SPEAKING 状态。
6. 播放完成后回到 IDLE。
7. 修改完成后运行 idf.py build。
```

### 15.9 优化提示词

```text
当前进入 V0.9。请做稳定性和体验优化。

要求：
1. 检查内存使用，避免大块堆内存泄漏。
2. 优化 LVGL 刷新频率。
3. 给 ASR、LLM、TTS 增加超时和错误码。
4. 增加网络断线恢复逻辑。
5. Debug 页面显示最近错误和当前 AI 状态。
6. 整理日志 Tag。
7. 修改完成后运行 idf.py build。
```

### 15.10 发布整理提示词

```text
当前进入 V1.0。请整理 MVP 发布版本。

要求：
1. 更新 README.md，说明环境、配置、编译、烧录、运行方式。
2. 更新 VERSION_ROADMAP.md，标记已完成功能和未完成功能。
3. 检查是否存在真实 Wi-Fi 密码或 API Key。
4. 清理无用代码和临时 Mock。
5. 确认 Home、AI Voice、Settings、Debug、About 页面可用。
6. 最后运行 idf.py build。
```

---

## 16. 每个版本完成后的检查清单

每完成一个版本，必须检查：

```text
[ ] 是否可以编译通过
[ ] 是否没有破坏已有功能
[ ] 是否更新了必要文档
[ ] 是否有清晰串口日志
[ ] 是否没有提交真实密钥
[ ] 是否符合模块化结构
[ ] 是否没有把业务逻辑塞进 UI 文件
[ ] 是否可以继续进入下一个版本
```

---

## 17. 不允许 Codex 做的事情

Codex 不允许：

1. 为了省事把所有代码写进一个文件。
2. 为了编译通过删除核心模块。
3. 在 UI 文件中直接写 HTTP/WebSocket 请求。
4. 在日志中打印完整 API Key。
5. 提交真实 Wi-Fi 密码。
6. 跳过版本路线直接实现大而全功能。
7. 把未完成的功能说成已完成。
8. 不运行构建就声称完成。
9. 用大量阻塞延时影响 UI。
10. 在音频任务中直接操作 LVGL 控件。

---

## 18. 项目最终验收标准

V1.0 最终验收标准：

1. 设备上电进入 Home 页面。
2. 触摸屏可正常操作。
3. Home 页面可进入 AI Voice、Settings、Debug、About 页面。
4. 每个页面可以返回 Home。
5. Wi-Fi 可以连接并显示状态。
6. 时间可以显示。
7. 串口输出完整调试日志。
8. 麦克风可以采集音频。
9. 扬声器可以播放音频。
10. AI Voice 能完成“用户说话 → ASR 识别 → LLM 回复 → TTS 播放”的闭环。
11. 网络或 API 失败时页面显示错误，不应崩溃。
12. 至少支持 3 轮简单上下文。
13. 项目结构清晰，后续可以添加新 App。
14. README 能指导新开发者编译和烧录。

---

## 19. 推荐开发顺序总结

严格按照以下顺序推进：

```text
V0.1 工程骨架
  ↓
V0.2 LCD + Touch + LVGL
  ↓
V0.3 页面路由
  ↓
V0.4 Wi-Fi + Time
  ↓
V0.5 Audio In/Out
  ↓
V0.6 ASR
  ↓
V0.7 LLM
  ↓
V0.8 TTS Playback
  ↓
V0.9 Stability
  ↓
V1.0 MVP Release
```

不要跳步。每一步都必须能独立编译、烧录、验证。

