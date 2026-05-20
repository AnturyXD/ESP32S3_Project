"""TTS 配置检查与语音合成接口。"""

from __future__ import annotations

from fastapi import APIRouter, Depends, HTTPException, Response, status
from fastapi.responses import JSONResponse

from app.core.config import get_settings
from app.core.security import require_device_auth
from app.schemas.tts import TtsSynthesizeRequest
from app.services.tts_proxy import (
    TtsConfigMissingError,
    TtsSynthesisError,
    TtsUnsupportedFormatError,
    synthesize_tts,
)

router = APIRouter(prefix="/api/esp-ai-terminal/tts", tags=["tts"])


@router.get("/config")
def get_tts_config() -> dict[str, object]:
    """返回 TTS 非敏感配置摘要。"""

    return get_settings().tts_public_config_summary()


@router.post("/synthesize")
def synthesize(request: TtsSynthesizeRequest, _: None = Depends(require_device_auth)) -> Response:
    """合成中文语音并返回二进制音频。

    成功时返回 PCM/WAV PCM 二进制；失败时返回 JSON。该接口复用 S0.4 的
    X-Device-Token 鉴权，ESP32 不需要也不允许保存火山 TTS 密钥。
    """

    settings = get_settings()
    if len(request.text) > settings.tts_max_text_chars:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail=f"text too long, max {settings.tts_max_text_chars} chars",
        )

    try:
        result = synthesize_tts(
            request.text,
            device_id=request.device_id,
            voice_type=request.voice_type,
            audio_format=request.audio_format,
            sample_rate=request.sample_rate,
        )
    except TtsConfigMissingError:
        return JSONResponse(
            status_code=status.HTTP_200_OK,
            content={"status": "Config Missing", "message": "TTS Config Missing"},
        )
    except TtsUnsupportedFormatError as exc:
        return JSONResponse(
            status_code=status.HTTP_415_UNSUPPORTED_MEDIA_TYPE,
            content={"status": "Unsupported Format", "message": str(exc)},
        )
    except ValueError as exc:
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail=str(exc)) from exc
    except TtsSynthesisError as exc:
        raise HTTPException(status_code=status.HTTP_502_BAD_GATEWAY, detail=str(exc)) from exc

    media_type = "audio/wav" if result.audio_format == "wav" else "application/octet-stream"
    return Response(
        content=result.audio,
        media_type=media_type,
        headers={
            "X-Audio-Format": result.audio_format,
            "X-Sample-Rate": str(result.sample_rate),
            "X-Bits": str(result.bits),
            "X-Channels": str(result.channels),
            "X-Audio-Bytes": str(len(result.audio)),
        },
    )
