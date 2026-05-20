# PROJECT_SPEC.md

# ESP32-S3 触屏 AI 语音终端项目方案与版本步骤

> 适用对象：Codex / AI 编程代理 / 后续开发者  
> 设备端：Waveshare ESP32-S3-Touch-LCD-3.49  
> 开发环境：VS Code + ESP-IDF  
> UI 框架：LVGL  
> 服务器：2 核 / 4G / 6M Linux 服务器，已有其他项目运行  
> 当前路线：ASR / LLM / TTS 主链路已实机跑通，后续进入 AI Voice 状态机稳定性和 MVP 演示闭环。

---

## 0. 文档职责

本文件只写 **项目方案、版本路线、详细开发步骤、验收标准、阶段提示词**。  
所有通用规范、禁止事项、密钥规则、模块边界、安全规则放在 `AGENTS.md`。

Codex 开始任何任务前必须先阅读：

```text
PROJECT_SPEC.md
AGENTS.md
```

---

## 1. 当前项目状态

### 1.1 已通过版本

```text
V0.1      工程骨架、模块组件、基础日志：PASS
V0.2      LCD / Touch / LVGL / Home 页面：PASS
V0.3      App Shell、页面路由、独立页面骨架：PASS
V0.3.x    返回按钮与页面切换花屏修复：PASS
V0.4      Wi-Fi STA、IP 获取、SNTP 时间同步：PASS
V0.4.x    UI 状态刷新链路修复：PASS
V0.4.5    PWR 电池供电控制：PASS
V0.5      音频 I/O 基础验证，测试音播放：PASS
V0.5.2    TCA9554 EXIO6 / EXIO7 共享状态保护：PASS
V0.5.3    SD 卡外部存储：PASS / 基本正常
S0.1      服务器只读审计与避让方案：PASS
S0.2-pre  本地 esp-ai-terminal-server/ 服务端骨架：PASS
S0.3      服务器最小运行 + 设备注册/心跳接口：PASS
S0.4      ESP32 真机注册与周期心跳上报：PASS
S0.5      服务器侧火山 ASR WebSocket smoke test：PASS
V0.6      ESP32 音频 → 自建服务器 → 火山 ASR → 识别结果返回设备：PASS / 主链路跑通
V0.6.1    ASR 稳定性收尾，不做中文显示：PASS / 实机验收通过
V0.7      LLM 文本对话代理：PASS / 实机验收通过
V0.8      TTS 中文语音播放：PASS / 实机验收通过
```

### 1.2 当前已验证能力

设备端：

1. ESP32 可通过 Wi-Fi 访问自建 FastAPI 服务器。
2. 设备注册和周期心跳链路正常。
3. `service_cloud` 已作为独立组件接入。
4. AI Voice 页面已支持 Start ASR / Stop ASR。
5. ESP32 麦克风 PCM 可上传到自建服务器。
6. AI Voice 页面可显示 ASR 状态、录音状态、发送时长、partial/final 文本。
7. Stop ASR 后业务状态能正确进入 Waiting Final，并最终进入 Done。
8. 云端心跳在 ASR 使用后仍能继续运行。
9. AI Voice 页面临时音频测试入口已清理：Record Test、Stop Record、Play Tone、Stop Play 均已删除。
10. 底层 `service_audio` 测试接口仍保留，方便后续排障。
11. 中文 ASR 已可通过串口日志确认，屏幕不显示中文原文。
12. 收到中文或非 ASCII 文本时，UI 采用英文状态降级显示，避免乱码。
13. 单次 ASR 最大录音时长限制保留，用于避免无意义上传和费用失控。

服务器端：

1. FastAPI 服务可运行。
2. S0.4 设备注册 / 心跳链路已通过。
3. S0.5 火山 ASR WebSocket smoke test 已通过。
4. 火山已接收 2 秒 16kHz / 16bit / mono PCM 测试音。
5. `ASR smoke test finished successfully` 已验证。
6. 服务器可转发 ESP32 音频到火山 ASR。
7. 英文内容已经可以被正确识别并返回设备。
8. 中文内容已经可以被正确识别并返回设备，串口日志可看到完整 UTF-8 文本。
9. 火山方舟 LLM 文本代理已接入。
10. ASR final text 已可发送到服务器 Chat 接口。
11. 服务器可调用火山方舟在线推理并返回中文回复文本。
12. ESP32 已可收到 `reply_text`，串口可看到完整 UTF-8 中文回复。
13. 屏幕不显示中文回复原文，继续使用英文状态降级显示。
14. 火山 TTS 代理已接入。
15. LLM 中文回复可发送到服务器 TTS 接口。
16. 服务器可返回 16kHz / 16bit / mono PCM 音频。
17. ESP32 可下载 TTS 音频并通过 `service_audio` 播放中文语音。
18. TTS 播放期间 Cloud heartbeat 仍继续返回 `200 OK`。

### 1.3 S0.5 关键结论

当前火山 ASR 正确配置为：

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

1. 当前使用 `ASR_APP_KEY + ASR_ACCESS_KEY` 鉴权。
2. 不再使用 `ASR_API_KEY` 单 Key 模式。
3. 正确资源 ID 为 `volc.bigasr.sauc.duration`。
4. `volc.seedasr.sauc.duration` 会返回 `requested resource not granted`。
5. 当前音频格式为 16kHz / 16bit / mono PCM。
6. 当前分包建议为 200ms。
7. 后续所有文档和代码不得恢复错误资源 ID 或错误鉴权字段。

### 1.4 当前已知现象

Stop ASR 后仍可能看到 ESP-IDF WebSocket 组件底层日志：

```text
transport_ws: Error read data(-1)
websocket_client: Error receive data
```

业务层已经正确识别为正常结束：

```text
AI: ASR websocket closed after stop, treat as normal finalizing
AI: ASR state -> Done
```

因此这属于非致命底层日志，不影响功能。

### 1.5 中文显示策略变更

当前不再要求设备屏幕正确显示中文内容。

新的目标：

```text
设备最终能够用中文语音回答。
屏幕只显示英文状态、ASCII 文本、数字、调试状态。
中文 ASR / LLM 文本可通过服务器日志或串口确认。
中文输出由 V0.8 TTS 播放承担。
```

### 1.6 V0.6.1 实机验收结论

```text
V0.6.1 PASS：ASR 稳定性收尾实机通过，可以进入 V0.7。
```

已确认：

1. Start ASR 可以正常进入录音上传。
2. Stop ASR 可以停止 PCM 采集并进入 `Waiting Final`。
3. 服务器返回 final 文本后，业务状态进入 `Done`。
4. Stop 后服务器主动关闭 WebSocket 时，ESP-IDF 底层仍可能打印 read error，但业务层会按正常收尾处理。
5. 中文 ASR 文本可通过串口确认，当前不要求屏幕显示中文原文。
6. AI Voice 页面不再暴露 Record Test / Stop Record / Play Tone / Stop Play 临时测试按钮。
7. ASR 会话结束后仍不影响 Cloud heartbeat。

实机日志基线：

```text
AI: ASR partial text: 你好，我叫胡图图，来自翻斗花园
AI: ASR stop requested
AUDIO_IN: pcm callback stopped capture by request
AUDIO_IN: pcm capture stopped
AI: ASR state -> Waiting Final
AI: ASR final text: 你好，我叫胡图图，来自翻斗花园。
AI: ASR websocket closed after stop, treat as normal finalizing
AI: ASR websocket disconnected
AI: ASR state -> Done
AI: ASR session ended, sent_seconds=7.28
```

当前保留的已知现象：

1. Stop 后可能出现 `transport_ws: Error read data(-1)` / `websocket_client: Error receive data`。
2. 该日志来自服务器正常 FIN 关闭后的 ESP-IDF WebSocket 底层读错误，不作为业务失败处理。
3. 屏幕不显示中文原文，这是当前产品策略，不作为缺陷处理。

### 1.7 V0.7 实机验收结论

```text
V0.7 PASS：LLM 文本对话代理实机通过，可以进入 V0.8。
```

已确认：

1. 服务器端 `POST /api/esp-ai-terminal/chat` 可用。
2. Chat 接口使用 `X-Device-Token` 鉴权，缺失或错误 token 会返回 `401`。
3. 服务器端已读取火山方舟 `ARK_API_KEY` 和 `LLM_MODEL`，但不会通过接口返回密钥原文。
4. 服务器可以调用火山方舟在线推理并返回中文 `reply_text`。
5. ESP32 在收到 ASR final text 后，可以自动请求服务器 Chat 接口。
6. ESP32 可以收到中文 LLM 回复文本，并通过串口打印完整 UTF-8 内容。
7. AI Voice 页面不要求显示中文原文，中文回复在屏幕上降级显示为英文状态。
8. V0.7 不接 TTS，不播放语音。
9. ASR 使用后 Cloud heartbeat 仍继续返回 `200 OK`。
10. 未修改 Carshow / Nginx / Docker / systemd / UFW。

服务器实测日志基线：

```text
LLM request: device_id=esp32s3-dev-001 provider=volcengine_ark language=zh input_chars=10 max_tokens=512
LLM reply: device_id=esp32s3-dev-001 text=我是基于ESP32-S3的中文语音助手，能通过语音和你交流互动。
POST /api/esp-ai-terminal/chat HTTP/1.1" 200 OK
```

ESP32 实测日志基线：

```text
AI: ASR final text: 自我介绍。
AI: ASR state -> Done
AI: LLM state -> Requesting
CLOUD: chat request: text_chars=15
CLOUD: POST /api/esp-ai-terminal/chat token_configured=yes
CLOUD: chat reply text: 你好，我是ESP32-S3 AI语音终端的中文语音助手。我能通过语音和你互动，帮你处理各种任务，有什么需求随时跟我说吧~
AI: LLM reply text: 你好，我是ESP32-S3 AI语音终端的中文语音助手。我能通过语音和你互动，帮你处理各种任务，有什么需求随时跟我说吧~
AI: LLM state -> Done
CLOUD: heartbeat ok, http_status=200
```

当前保留的已知现象：

1. Stop ASR 后仍可能出现 ESP-IDF WebSocket 底层 FIN read error。
2. 这仍是非致命日志，业务层会正常进入 `Done`。
3. 中文 LLM 回复不在屏幕显示原文，后续由 V0.8 TTS 完成中文输出。

### 1.8 V0.8 实机验收结论

```text
V0.8 PASS：TTS 中文语音播放实机通过，可以进入 V0.9。
```

已确认：

1. 服务器端 `GET /api/esp-ai-terminal/tts/config` 可用。
2. 服务器端 `POST /api/esp-ai-terminal/tts/synthesize` 可用。
3. TTS 合成接口复用 `X-Device-Token` 鉴权，缺失或错误 token 会返回 `401`。
4. 服务器可调用火山 TTS / 豆包语音合成 2.0，并返回设备可播放音频。
5. 当前实测返回音频格式为 16kHz / 16bit / mono PCM。
6. ESP32 收到 LLM `reply_text` 后可自动请求 TTS。
7. ESP32 可下载 TTS PCM 音频，并通过 `service_audio` 播放中文语音。
8. mono PCM 播放前继续在设备端复制到 L/R 双声道槽位，适配当前 ES8311 播放链路。
9. AI Voice 页面可显示 TTS State / Speaking / TTS Bytes / Done。
10. 中文回复仍不在屏幕显示原文，中文输出由 TTS 语音承担。
11. TTS 播放期间 UI 不阻塞，Cloud heartbeat 仍继续返回 `200 OK`。
12. 未引入中文字体。
13. 未接 Doubao Realtime API。
14. 未修改 ASR / LLM 已通过配置。
15. 未修改 Carshow / Nginx / Docker / systemd / UFW。

服务器侧 TTS 验证基线：

```text
python scripts/tts_smoke_test.py
TTS smoke test start
provider=volcengine
model=seed-tts-2.0
api_version=v3
audio=16000Hz/16bit/1ch format=pcm
credential_configured=True
```

ESP32 实测日志基线：

```text
AI: ASR final text: 做一个自我介绍。
AI: LLM state -> Requesting
CLOUD: POST /api/esp-ai-terminal/chat token_configured=yes
CLOUD: chat reply text: 你好呀！我是你的ESP32-S3 AI语音助手，能听懂你的语音指令，帮你查信息、设提醒，还能用语音回答你问题哦。有什么需要帮忙的，随时说吧~
AI: LLM state -> Done
AI: TTS state -> Requesting
AI: TTS state -> Downloading
CLOUD: POST /api/esp-ai-terminal/tts/synthesize for TTS token_configured=yes
CLOUD: TTS audio downloaded: bytes=397176 format=pcm sample_rate=16000 bits=16 channels=1
AI: TTS state -> Playing
AUDIO_OUT: pcm playback started: bytes=397176 sample_rate=16000 bits=16 channels=1
CLOUD: heartbeat ok, http_status=200
AUDIO_OUT: pcm playback stopped: result=ESP_OK
AI: TTS state -> Done
CLOUD: heartbeat ok, http_status=200
```

当前保留的已知现象：

1. Stop ASR 后仍可能出现 ESP-IDF WebSocket 底层 FIN read error，仍按非致命日志处理。
2. 屏幕仍不显示中文原文，只显示英文状态或降级提示。
3. V0.8 当前使用 HTTP 完整音频下载方案，后续可在 V0.8.x 或 V0.9 后评估流式 TTS。
4. TTS 音频大小已有限制，后续仍需在 V0.9 统一状态机中强化取消、超时和连续多轮对话处理。

---

## 2. 最新紧凑版本路线

```text
V0.6      ESP32 音频 → 服务器 → 火山 ASR → 识别结果返回设备：PASS
V0.6.1    ASR 稳定性收尾，不做中文显示：PASS
V0.7      LLM 文本对话代理：PASS
V0.8      TTS 中文语音播放：PASS
V0.9      AI Voice 状态机稳定性
V1.0      MVP 演示版
```

路线说明：

1. V0.6 已经跑通主链路。
2. V0.6.1 已完成稳定性收尾，不做中文字库。
3. V0.7 已经接入 LLM，服务器和设备端都能拿到回复文本，不要求屏幕显示中文。
4. V0.8 已经接入 TTS，设备可以播放中文语音回复。
5. 当前下一步是 V0.9：做完整 AI Voice 状态机、取消/超时/异常恢复和连续多轮对话稳定性。
6. V1.0 做完整演示闭环。

---

## 3. V0.6.1：ASR 稳定性收尾

### 3.1 目标

V0.6.1 是小版本，只整理 ASR 主链路稳定性。

目标：

1. 稳定 Start ASR / Stop ASR / Done / Error 状态流转。
2. 保留 V0.6 已跑通的音频上传和 ASR 返回能力。
3. 保留业务层对 WebSocket 正常关闭的判断。
4. 加强最大录音时长、发送时长统计、重复点击保护。
5. 清理 UI 状态显示。
6. 不引入中文字库。
7. 不要求屏幕显示中文。
8. 不接 LLM。
9. 不接 TTS。

### 3.2 本轮允许做

设备端：

1. 修改 `service_ai` 状态机。
2. 修改 `service_cloud` ASR WebSocket 相关状态。
3. 修改 AI Voice 页面状态显示。
4. 修改 Debug 页面只读状态显示。
5. 修复 Start / Stop 重复点击。
6. 增加或完善 ASR sent seconds 统计。
7. 增加或完善最大录音时长限制。
8. 优化非致命 WebSocket 关闭日志。

服务器端：

1. 原则上不修改服务器 ASR 协议。
2. 可以只补充文档或日志说明。
3. 不修改已通过的 ASR 鉴权和资源 ID。

### 3.3 本轮禁止做

1. 不做中文字库。
2. 不做屏幕中文显示。
3. 不接 LLM。
4. 不接 TTS。
5. 不修改火山 ASR 已通过配置。
6. 不恢复 `ASR_API_KEY` 单 Key 模式。
7. 不恢复 `volc.seedasr.sauc.duration`。
8. 不修改 Nginx / UFW / Docker / systemd / Carshow。
9. 不提交真实密钥。
10. 不恢复 AI Voice 页面临时音频测试按钮。

### 3.4 状态机要求

建议状态：

```text
AI_ASR_STATE_IDLE
AI_ASR_STATE_CONNECTING
AI_ASR_STATE_RECORDING
AI_ASR_STATE_WAITING_FINAL
AI_ASR_STATE_DONE
AI_ASR_STATE_ERROR
```

状态流转要求：

1. 用户点击 Start ASR 后才进入 `CONNECTING`。
2. WebSocket 连接成功后进入 `RECORDING`。
3. 用户点击 Stop ASR 后进入 `WAITING_FINAL` 或 `DONE`。
4. 如果 `stop_requested=true` 后 WebSocket 被服务器主动关闭，不应进入 `ERROR`。
5. 只有非预期断开、鉴权失败、服务器返回 error、音频采集失败才进入 `ERROR`。
6. `DONE` 状态下允许再次点击 Start。
7. Start ASR 期间禁止重复 Start。
8. Stop ASR 期间重复 Stop 不应导致崩溃。
9. 单次录音最大时长限制必须保留。
10. 发送音频秒数要在每次 Start 时清零。

### 3.5 AI Voice 页面要求

AI Voice 页面保留：

```text
Start ASR
Stop ASR
ASR State
Recording
Sent Seconds
Partial Text / Text Received 状态
Final Text / Final Received 状态
Last Error
```

不恢复：

```text
Record Test
Stop Record
Play Tone
Stop Play
```

中文处理：

1. 如果收到中文 ASR 文本，页面不要求显示中文。
2. 可以显示 `Chinese text received`。
3. 串口和服务器日志可保留完整中文。
4. 不引入中文字体。

### 3.6 验收标准

```text
[ ] 设备启动正常
[ ] Wi-Fi / Time 正常
[ ] Cloud register / heartbeat 正常
[ ] AI Voice 页面只保留 ASR 正式入口
[ ] 英文 ASR 仍然可识别
[ ] 中文 ASR 可通过串口或服务器日志确认
[ ] 屏幕不要求显示中文
[ ] Stop ASR 后业务状态进入 Done
[ ] Done 后可以再次 Start
[ ] Start / Stop 重复点击不崩溃
[ ] 单次录音最大时长限制有效
[ ] 不影响 PWR / Audio / Storage
[ ] idf.py build 通过
```

### 3.7 给 Codex 的 V0.6.1 提示词

```text
当前进入 V0.6.1：ASR 稳定性收尾，不做中文显示。

请先阅读：
1. PROJECT_SPEC.md
2. AGENTS.md

当前状态：
1. V0.6 主链路已跑通。
2. ESP32 可通过 Wi-Fi 访问自建 FastAPI 服务器。
3. 设备注册 / 心跳正常。
4. 服务器侧火山 ASR smoke test 已通过。
5. ESP32 麦克风 PCM 可上传到服务器。
6. 服务器可转发音频到火山 ASR。
7. 英文 ASR 已可正确识别并返回设备。
8. AI Voice 页面可显示 ASR 状态、录音状态、发送时长、partial/final 文本。
9. Stop ASR 后业务状态可正常进入 Done。
10. 当前已清理 AI Voice 页面临时音频测试入口。
11. 当前不要求屏幕显示中文。
12. 中文 ASR 文本可通过服务器日志或串口确认。
13. 最终中文输出目标放到 V0.8 TTS。

本轮目标：
1. 稳定 ASR Start / Stop / Done / Error 状态流转。
2. 保持 V0.6 主链路不被破坏。
3. 不接 LLM。
4. 不接 TTS。
5. 不做中文字库。
6. 不要求屏幕显示中文。
7. 不修改服务器 ASR 资源 ID、鉴权方式和 smoke test 已通过的配置。
8. 不修改 Carshow / Nginx / Docker / systemd / UFW。
9. 不提交真实 Wi-Fi、服务器地址、DEVICE_SHARED_SECRET、ASR_APP_KEY、ASR_ACCESS_KEY。

请完成：
1. 梳理 AI_ASR_STATE 状态机。
2. Stop 后服务器主动关闭 WebSocket 时不进入 ERROR。
3. 只有非预期断开、鉴权失败、服务器 error、音频采集失败才进入 ERROR。
4. Done 状态下允许再次 Start。
5. Start 期间禁止重复 Start。
6. Stop 期间重复 Stop 不崩溃。
7. 每次 Start 清零 Sent Seconds。
8. 保留单次最大录音时长限制。
9. AI Voice 页面只保留 Start ASR / Stop ASR / ASR 状态相关显示。
10. 如果收到中文文本，UI 可显示 Chinese text received，不引入中文字库。
11. 串口日志可打印中文 ASR 文本。
12. 不恢复 Record Test / Stop Record / Play Tone / Stop Play 按钮。
13. 底层 service_audio 测试接口继续保留。

完成后运行：
idf.py build

完成后汇报：
1. 修改了哪些文件。
2. ASR 状态机是否整理。
3. Stop 后 WebSocket 底层 error 是否仍存在，是否为非致命。
4. 是否没有做中文字体。
5. 是否没有接 LLM / TTS。
6. 是否没有修改服务器 ASR 已通过配置。
7. build 是否通过。
8. 下一步是否可以进入 V0.7。
```

---

## 4. V0.7：LLM 文本对话代理

### 4.1 目标

V0.7 目标是把 ASR final text 发送到服务器，由服务器调用火山方舟 LLM，返回回复文本。

链路：

```text
用户语音
  ↓
ESP32 录音
  ↓
服务器 ASR
  ↓
ASR final text
  ↓
服务器 LLM 代理
  ↓
火山方舟在线推理（常规）
  ↓
LLM 回复文本
  ↓
ESP32 收到回复状态
```

V0.7 不做 TTS，不要求屏幕显示中文回复。

### 4.2 服务器端目标

新增接口：

```http
POST /api/esp-ai-terminal/chat
```

请求示例：

```json
{
  "device_id": "esp32s3-dev-001",
  "text": "今天天气怎么样",
  "conversation_id": "default",
  "turn_id": "uuid"
}
```

返回示例：

```json
{
  "status": "ok",
  "reply_text": "我现在还不能直接查询实时天气，但我可以帮你分析天气查询功能应该怎么接入。",
  "conversation_id": "default",
  "turn_id": "uuid"
}
```

服务器要求：

1. 使用火山方舟在线推理（常规）。
2. 使用 `ARK_API_KEY`、`ARK_BASE_URL`、`LLM_MODEL`。
3. `LLM_MODEL` 填推理接入点 ID。
4. 不在服务器本地跑 LLM。
5. 不打印 `ARK_API_KEY`。
6. `/audit/runtime` 只显示 `ark_api_key_configured: true/false`。
7. 如果 LLM 配置缺失，返回 `Config Missing`，不崩溃。
8. 每次请求设置超时。
9. 限制输入文本长度。
10. 限制输出 token 数。
11. 后续对话上下文先做最小内存实现，不接数据库。
12. 服务器日志可以记录脱敏后的对话状态，不要记录密钥。

### 4.3 设备端目标

推荐模式 A：ASR 完成后手动触发 Chat。

1. AI Voice 页面显示 ASR Done。
2. 增加或保留一个 `Send to AI` / `Ask AI` 按钮。
3. 点击后将 ASR final text 发送到服务器 `/chat`。
4. 收到回复后 UI 显示 `Response received` 或 `Reply ready`。
5. 不要求屏幕显示中文回复。

可选模式 B：ASR final 后自动 Chat。

1. ASR final text 收到后自动调用 `/chat`。
2. 适合演示，但不利于调试。
3. 如果采用自动模式，必须能在配置中关闭。

建议优先采用模式 A。

### 4.4 屏幕显示要求

AI Voice 页面显示：

```text
ASR State
LLM State
Last ASR Text: English or text received
Reply State: Response received / Reply ready / Error
Last Error
```

中文处理：

1. 中文回复不要求显示在屏幕上。
2. 可以显示 `Chinese reply ready`。
3. 完整中文回复可以在串口或服务器日志中确认。
4. V0.8 负责把中文回复播放出来。

### 4.5 V0.7 验收标准

```text
[x] S0.4 register / heartbeat 仍正常
[x] V0.6 ASR 仍正常
[x] 服务器 /api/esp-ai-terminal/chat 可用
[x] LLM 配置缺失时返回 Config Missing
[x] LLM 配置完成后可以返回文本回复
[x] 不泄露 ARK_API_KEY
[x] 设备端可以把 ASR final text 发送到 /chat
[x] 设备端能收到回复状态
[x] 屏幕不要求显示中文回复
[x] 不接 TTS
[x] 不影响 PWR / Audio / Storage / Time / Wi-Fi
[x] idf.py build 通过
```

### 4.6 给 Codex 的 V0.7 提示词

```text
当前进入 V0.7：LLM 文本对话代理。

请先阅读：
1. PROJECT_SPEC.md
2. AGENTS.md

当前状态：
1. V0.6 主链路已跑通。
2. V0.6.1 已完成或至少 ASR 主链路稳定。
3. ESP32 可以录音并通过服务器获得 ASR final text。
4. 服务器火山 ASR 使用 ASR_APP_KEY + ASR_ACCESS_KEY。
5. ASR_RESOURCE_ID=volc.bigasr.sauc.duration。
6. 本轮接入 LLM 文本对话代理。
7. 本轮不接 TTS。
8. 本轮不要求屏幕显示中文。
9. 中文回复最终通过 V0.8 TTS 播放。

本轮目标：
1. 服务器新增 /api/esp-ai-terminal/chat。
2. 服务器接入火山方舟在线推理（常规）。
3. 设备端可以把 ASR final text 发送到服务器 chat。
4. 设备端收到回复状态。
5. 不泄露 ARK_API_KEY。
6. 不修改 Nginx / UFW / Docker / systemd / Carshow。
7. 不提交真实密钥。

服务器端请完成：
1. .env.example 增加：
   LLM_PROVIDER=volcengine_ark
   ARK_API_KEY=
   ARK_BASE_URL=https://ark.cn-beijing.volces.com/api/v3
   LLM_MODEL=
   LLM_MAX_INPUT_CHARS=500
   LLM_MAX_OUTPUT_TOKENS=256
   LLM_TIMEOUT_SECONDS=20
2. app/core/config.py 增加 LLM 配置。
3. 新增 app/api/chat.py。
4. 新增或完善 app/services/llm_proxy.py。
5. 实现 POST /api/esp-ai-terminal/chat。
6. 缺少配置时返回 Config Missing。
7. 调用失败时返回明确错误。
8. 日志不打印 ARK_API_KEY。
9. /audit/runtime 只显示 ark_api_key_configured: true/false。

设备端请完成：
1. service_ai 增加 LLM 状态。
2. service_cloud 增加 chat HTTP POST。
3. AI Voice 页面增加 Ask AI / Send to AI 入口，或明确采用 ASR final 后自动 Chat。
4. 推荐先采用手动 Ask AI。
5. 收到 LLM 回复后，屏幕只显示 Response received / Reply ready。
6. 不要求显示中文回复。
7. 串口可以打印中文回复用于调试。
8. 不接 TTS。

完成后运行：
服务器：python -m compileall app
设备：idf.py build

完成后汇报：
1. 服务器端修改了哪些文件。
2. 设备端修改了哪些文件。
3. /api/esp-ai-terminal/chat 是否可用。
4. 是否成功调用火山方舟。
5. 是否没有泄露 ARK_API_KEY。
6. 是否没有接 TTS。
7. 是否没有要求屏幕显示中文。
8. build 是否通过。
9. 下一步是否可以进入 V0.8。
```

---

## 5. V0.8：TTS 中文语音播放

### 5.1 目标

V0.8 是中文输出能力的核心版本。

目标链路：

```text
ASR final text
  ↓
LLM 中文回复
  ↓
服务器 TTS 代理
  ↓
火山 TTS / 豆包语音合成 2.0
  ↓
返回音频
  ↓
ESP32 播放中文语音
```

本阶段重点不是屏幕显示中文，而是设备可以正确播放中文回复语音。

### 5.2 服务器端目标

新增接口，二选一：

#### 方案 A：HTTP 返回完整音频

```http
POST /api/esp-ai-terminal/tts/synthesize
```

优点：实现简单，适合先跑通。  
缺点：延迟较高，长回复需要缓存。

#### 方案 B：WebSocket 流式 TTS

```text
WS /ws/esp-ai-terminal/tts
```

优点：延迟更低，更接近电话式体验。  
缺点：状态机复杂，设备播放缓冲更复杂。

建议：

```text
V0.8 先用 HTTP 完整音频方案跑通。
V0.8.x 再优化为 WebSocket 流式播放。
```

### 5.3 设备端目标

1. 设备发送 LLM reply text 或 reply_id 到服务器。
2. 服务器返回音频。
3. 设备播放音频。
4. 音频播放不阻塞 UI task。
5. TTS 音频可以短期缓存到 SD 卡。
6. 播放结束后状态进入 Done。
7. 播放失败进入 Error。
8. 中文语音播放清晰可辨。

### 5.4 V0.8 验收标准

```text
[x] V0.7 LLM 回复正常
[x] 服务器 TTS 配置检查接口可用
[x] 服务器 TTS 合成接口可用
[x] 服务器可以调用火山 TTS
[x] 服务器不泄露 TTS 密钥
[x] ESP32 能接收 TTS 音频
[x] ESP32 能播放中文语音
[x] 播放期间 UI 不阻塞
[ ] Stop / Cancel 可中断播放或进入安全状态
[x] TTS 音频下载大小受限
[x] 屏幕不要求显示中文
[x] idf.py build 通过
```

### 5.5 V0.8 实现结论

```text
V0.8 已采用 HTTP 完整音频方案跑通。
服务器接口：POST /api/esp-ai-terminal/tts/synthesize
设备播放格式：16kHz / 16bit / mono PCM
实测音频大小：397176 bytes
实测播放结果：ESP_OK
```

V0.8 暂不解决的问题：

1. 不做 TTS WebSocket 流式播放。
2. 不做声音复刻。
3. 不做长期音频缓存。
4. 不引入中文字体。
5. 不做统一 AI Voice 大状态机，统一状态机放到 V0.9。

---

## 6. V0.9：AI Voice 状态机稳定性

目标是把 ASR / LLM / TTS 统一成完整状态机。

建议状态：

```text
AI_STATE_IDLE
AI_STATE_LISTENING
AI_STATE_ASR_CONNECTING
AI_STATE_ASR_RECORDING
AI_STATE_ASR_WAITING_FINAL
AI_STATE_LLM_REQUESTING
AI_STATE_LLM_DONE
AI_STATE_TTS_REQUESTING
AI_STATE_TTS_PLAYING
AI_STATE_DONE
AI_STATE_ERROR
AI_STATE_CANCELING
```

必须处理：

1. 用户主动取消。
2. Wi-Fi 断开。
3. 服务器不可达。
4. ASR 超时。
5. LLM 超时。
6. TTS 超时。
7. 音频采集失败。
8. 音频播放失败。
9. SD 卡不可用。
10. 设备重启后恢复初始状态。
11. 多次连续对话。
12. 页面退出时如何处理正在进行的任务。

---

## 7. V1.0：MVP 演示版

完整演示目标：

```text
用户点击 AI Voice
↓
点击 Start
↓
用户用中文说一句话
↓
设备录音并上传服务器
↓
服务器 ASR 得到中文文本
↓
服务器 LLM 生成中文回复
↓
服务器 TTS 生成中文音频
↓
设备播放中文语音回复
↓
Debug 页面可查看状态
```

V1.0 验收标准：

```text
[ ] 用户可以用中文提问
[ ] 设备可以正确录音
[ ] ASR 可以识别中文
[ ] LLM 可以生成中文回复
[ ] TTS 可以播放中文回复
[ ] 屏幕显示状态正常
[ ] 不要求屏幕显示中文正文
[ ] 连续 3 轮对话不崩溃
[ ] Wi-Fi / Time / Storage / Audio / Power 不受影响
[ ] 设备密钥不泄露
[ ] 火山密钥只在服务器端
[ ] 服务器日志不打印完整密钥
```

---

## 8. 长期优化方向

V1.0 后再考虑：

1. HTTPS 反向代理替代裸露 18080。
2. Nginx / Caddy WebSocket 反向代理。
3. 设备证书或 JWT。
4. TTS WebSocket 流式播放。
5. VAD 静音检测。
6. 对话上下文持久化。
7. OTA 元数据下发。
8. Web 管理后台。
9. 多设备管理。
10. 端到端 Realtime API 方案评估。

---

## 9. 当前下一步建议

当前操作：

```text
V0.8 小结与文档同步，TTS 中文语音播放已实机通过。
```

建议下一步执行：

```text
V0.9：AI Voice 状态机稳定性
```

当前前置条件：

```text
V0.8 已实机验收通过。
```

推荐继续保持以下顺序：

```text
V0.8 → V0.9 → V1.0
```
