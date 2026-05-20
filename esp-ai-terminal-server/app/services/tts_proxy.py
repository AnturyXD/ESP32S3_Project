"""火山 TTS 代理服务。

V0.8 采用服务器代理方案：ESP32 只把 LLM reply_text 发给自建服务器，
真实火山 TTS 密钥只保存在服务器本地 `.env`。服务器向火山 TTS 请求
16kHz / 16bit / mono PCM 或 WAV PCM，再把音频返回 ESP32 播放。
"""

from __future__ import annotations

import base64
import json
import logging
import urllib.error
import urllib.request
from dataclasses import dataclass

from app.core.config import Settings, get_settings

logger = logging.getLogger(__name__)


class TtsConfigMissingError(RuntimeError):
    """TTS 配置缺失。API 层会转换成 Config Missing 响应。"""


class TtsUnsupportedFormatError(RuntimeError):
    """TTS 返回或请求了 ESP32 当前不可播放的格式。"""


class TtsSynthesisError(RuntimeError):
    """TTS 合成失败。错误信息已经过脱敏处理。"""


@dataclass
class TtsAudioResult:
    """TTS 合成结果。"""

    audio: bytes
    audio_format: str
    sample_rate: int
    bits: int
    channels: int


def get_tts_status() -> dict[str, object]:
    """返回 TTS 代理能力摘要，供旧占位接口展示。"""

    settings = get_settings()
    return {
        "enabled": settings.tts_configured,
        "provider": settings.tts_provider,
        "message": "V0.8 proxies text to Volcengine TTS",
    }


def _build_tts_headers(settings: Settings) -> dict[str, str]:
    """构造火山 TTS HTTP Header。

    新版控制台通常使用 X-Api-Key + X-Api-Resource-Id；部分旧控制台或
    Demo 使用 X-Api-App-Id + X-Api-Access-Key。两种方式都不在日志里
    打印密钥原文。
    """

    headers = {
        "Content-Type": "application/json",
        "X-Api-Resource-Id": settings.tts_resource_id.strip(),
        "X-Api-Request-Id": "esp-ai-terminal-tts",
    }
    if settings.tts_api_key_configured:
        headers["X-Api-Key"] = settings.tts_api_key.strip()
    else:
        headers["X-Api-App-Id"] = settings.tts_app_id.strip()
        headers["X-Api-Access-Key"] = settings.tts_access_token.strip()
    return headers


def _build_tts_payload(
    settings: Settings,
    *,
    device_id: str,
    text: str,
    voice_type: str,
    audio_format: str,
    sample_rate: int,
) -> dict[str, object]:
    """构造火山 TTS V3 HTTP Chunked 请求体。

    V0.8 只请求 ESP32 可直接播放的 PCM/WAV PCM，不请求 MP3/Opus/Ogg，
    避免设备端引入解码器或 ffmpeg 依赖。
    """

    req_params: dict[str, object] = {
        "text": text,
        "speaker": voice_type,
        "audio_params": {
            "format": audio_format,
            "sample_rate": sample_rate,
            "enable_timestamp": False,
        },
    }

    # 控制台字段 TTS_MODEL 保留给文档和资源摘要；只有官方模型变体名才放入请求体。
    if settings.tts_model in {"seed-tts-2.0-standard", "seed-tts-2.0-expressive"}:
        req_params["model"] = settings.tts_model

    return {
        "user": {"uid": device_id},
        "namespace": "BidirectionalTTS",
        "req_params": req_params,
    }


def _decode_chunk_line(line: bytes) -> dict[str, object] | None:
    """解析 HTTP Chunked/SSE 风格的一行 JSON。

    火山接口可能返回纯 JSON 行，也可能返回 `data: {...}` 风格。这里兼容
    两种格式；空行直接跳过。
    """

    text = line.decode("utf-8", errors="replace").strip()
    if not text:
        return None
    if text.startswith("data:"):
        text = text[5:].strip()
    if not text:
        return None
    return json.loads(text)


def _extract_audio_from_response(response: urllib.response.addinfourl) -> bytes:
    """从火山 TTS chunked 响应中提取 base64 音频分片。"""

    audio_parts: list[bytes] = []
    while True:
        line = response.readline()
        if not line:
            break
        payload = _decode_chunk_line(line)
        if payload is None:
            continue

        code = payload.get("code")
        message = str(payload.get("message", ""))
        if code not in (None, 0, 20000000):
            raise TtsSynthesisError(f"TTS service error: code={code} message={message[:120]}")

        data = payload.get("data")
        if isinstance(data, str) and data:
            try:
                audio_parts.append(base64.b64decode(data))
            except Exception as exc:  # noqa: BLE001
                raise TtsSynthesisError("TTS base64 decode failed") from exc

        if code == 20000000 or payload.get("is_last") is True:
            break

    return b"".join(audio_parts)


def synthesize_tts(
    text: str,
    *,
    device_id: str,
    voice_type: str = "",
    audio_format: str = "pcm",
    sample_rate: int = 16000,
) -> TtsAudioResult:
    """调用火山 TTS 并返回 ESP32 可播放的音频。

    该函数只接受 pcm/wav，且只允许 16kHz/16bit/mono。任何压缩格式都返回
    Unsupported Format，避免设备端盲目播放噪声。
    """

    settings = get_settings()
    if not settings.tts_configured:
        raise TtsConfigMissingError("TTS Config Missing")

    cleaned_text = text.strip()
    if len(cleaned_text) > settings.tts_max_text_chars:
        raise ValueError(f"text too long, max {settings.tts_max_text_chars} chars")

    fmt = (audio_format or settings.tts_audio_format).strip().lower()
    if fmt not in {"pcm", "wav"}:
        raise TtsUnsupportedFormatError(f"unsupported audio format: {fmt}")

    sr = sample_rate or settings.tts_sample_rate
    if sr != settings.tts_sample_rate or sr != 16000 or settings.tts_bits != 16 or settings.tts_channels != 1:
        raise TtsUnsupportedFormatError("V0.8 only supports 16kHz/16bit/mono PCM or WAV PCM")

    speaker = (voice_type or settings.tts_voice_type).strip()
    if not speaker:
        raise TtsConfigMissingError("TTS voice type missing")

    payload = _build_tts_payload(
        settings,
        device_id=device_id,
        text=cleaned_text,
        voice_type=speaker,
        audio_format=fmt,
        sample_rate=sr,
    )
    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    request = urllib.request.Request(
        settings.tts_effective_url,
        data=data,
        method="POST",
        headers=_build_tts_headers(settings),
    )

    logger.info(
        "TTS request: device_id=%s provider=%s format=%s sample_rate=%d text_chars=%d auth_mode=%s",
        device_id,
        settings.tts_provider,
        fmt,
        sr,
        len(cleaned_text),
        settings.tts_auth_mode,
    )

    try:
        with urllib.request.urlopen(request, timeout=settings.tts_timeout_seconds) as response:
            audio = _extract_audio_from_response(response)
    except urllib.error.HTTPError as exc:
        body_prefix = exc.read(300).decode("utf-8", errors="replace")
        logger.warning("TTS HTTP error: status=%s body=%s", exc.code, body_prefix)
        raise TtsSynthesisError(f"TTS HTTP {exc.code}") from exc
    except TtsSynthesisError:
        raise
    except Exception as exc:  # noqa: BLE001
        logger.warning("TTS request failed: %s", type(exc).__name__)
        raise TtsSynthesisError(f"TTS request failed: {type(exc).__name__}") from exc

    if not audio:
        raise TtsSynthesisError("TTS empty audio")

    logger.info("TTS audio ready: device_id=%s bytes=%d format=%s", device_id, len(audio), fmt)
    return TtsAudioResult(
        audio=audio,
        audio_format=fmt,
        sample_rate=sr,
        bits=16,
        channels=1,
    )
