"""FastAPI 应用入口。

S0.5 允许用户手动前台运行服务端 ASR smoke test 相关接口。默认仍建议绑定
127.0.0.1:18080；如临时公网调试 0.0.0.0:18080，必须由用户自行确认
安全组、UFW 和共享密钥。Codex 不直接连接服务器、不自动部署。
"""

from fastapi import FastAPI

from app.api import ai_proxy, asr, devices, health, runtime, version
from app.core.config import get_settings
from app.core.logging_config import setup_logging

settings = get_settings()
setup_logging(settings.log_level)

app = FastAPI(
    title=settings.app_name,
    version=settings.app_version,
    description="ESP32-S3 AI voice terminal lightweight gateway skeleton",
)

# 使用 APIRouter 保持接口模块边界清晰，后续 ASR/LLM/TTS 扩展不会堆到 main.py。
app.include_router(health.router)
app.include_router(version.router)
app.include_router(runtime.router)
app.include_router(devices.router)
app.include_router(ai_proxy.router)
app.include_router(asr.router)

