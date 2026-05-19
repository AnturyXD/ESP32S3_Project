"""设备侧 Chat 接口。

V0.7 只实现 ASR final text -> 火山方舟 LLM -> reply_text 的单轮链路。
接口需要复用 S0.4 的 X-Device-Token 鉴权；真实 ARK_API_KEY 只在服务器
本地 `.env` 中读取，不会返回给设备或写入日志。
"""

from __future__ import annotations

from fastapi import APIRouter, Depends, HTTPException, status

from app.core.config import get_settings
from app.core.security import require_device_auth
from app.schemas.chat import ChatRequest, ChatResponse
from app.services.llm_proxy import LlmConfigMissingError, request_chat_reply

router = APIRouter(prefix="/api/esp-ai-terminal", tags=["chat"])


@router.post("/chat", response_model=ChatResponse)
def chat(request: ChatRequest, _: None = Depends(require_device_auth)) -> ChatResponse:
    """调用火山方舟生成单轮回复。

    text 长度使用服务器配置限制，避免 ESP32 异常或恶意请求导致 token 消耗失控。
    本轮不保存长期会话历史、不接 TTS、不返回音频。
    """

    settings = get_settings()
    if len(request.text) > settings.llm_max_input_chars:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail=f"text too long, max {settings.llm_max_input_chars} chars",
        )

    try:
        reply_text = request_chat_reply(
            request.text,
            device_id=request.device_id,
            language=request.language,
        )
    except LlmConfigMissingError:
        return ChatResponse(
            status="Config Missing",
            device_id=request.device_id,
            reply_received=False,
            message="LLM Config Missing",
        )
    except ValueError as exc:
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail=str(exc)) from exc
    except Exception as exc:  # noqa: BLE001
        raise HTTPException(status_code=status.HTTP_502_BAD_GATEWAY, detail=str(exc)) from exc

    return ChatResponse(
        status="ok",
        device_id=request.device_id,
        reply_text=reply_text,
        reply_received=True,
    )
