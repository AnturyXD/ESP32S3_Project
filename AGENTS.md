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