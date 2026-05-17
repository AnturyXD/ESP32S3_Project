"""AI 代理占位 API。

本模块只暴露 ASR/LLM/TTS 的占位状态。真实云端 API、WebSocket 转发、
密钥读取和请求签名都留到后续版本，避免 S0.4 越界。
"""

from fastapi import APIRouter

from app.services.asr_proxy import get_asr_status
from app.services.llm_proxy import get_llm_status
from app.services.tts_proxy import get_tts_status

router = APIRouter(prefix="/api/v1/ai", tags=["ai-proxy"])


@router.get("/status")
def ai_proxy_status() -> dict[str, object]:
    """返回 AI 代理占位能力状态。"""

    return {
        "asr": get_asr_status(),
        "llm": get_llm_status(),
        "tts": get_tts_status(),
        "websocket": {
            "enabled": False,
            "message": "WebSocket directory and protocol are reserved for future ASR/TTS proxy",
        },
    }

