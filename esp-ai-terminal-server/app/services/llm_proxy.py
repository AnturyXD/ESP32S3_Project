"""火山方舟 LLM 代理服务。

V0.7 开始，ESP32 只把 ASR final text 发给自建服务器，真实火山方舟
`ARK_API_KEY` 只保存在服务器本地 `.env`。这样可以避免把云端密钥烧录到
ESP32，也方便后续在服务器侧做限流、日志脱敏和 HTTPS 入口。
"""

from __future__ import annotations

import json
import logging
import urllib.error
import urllib.request

from app.core.config import Settings, get_settings

logger = logging.getLogger(__name__)


class LlmConfigMissingError(RuntimeError):
    """LLM 配置缺失。

    该异常会被 API 层转换成 Config Missing 响应，而不是让服务器崩溃。
    """


def get_llm_status() -> dict[str, object]:
    """返回 LLM 代理能力摘要，供旧占位接口展示。"""

    settings = get_settings()
    return {
        "enabled": settings.llm_configured,
        "provider": settings.llm_provider,
        "message": "V0.7 proxies text chat to Volcengine Ark LLM",
    }


def _build_chat_url(settings: Settings) -> str:
    """构造 OpenAI-compatible chat completions 地址。"""

    base = settings.ark_base_url.rstrip("/")
    return f"{base}/chat/completions"


def _extract_reply_text(payload: dict[str, object]) -> str:
    """从火山方舟兼容 OpenAI 的响应中提取回复文本。"""

    choices = payload.get("choices")
    if not isinstance(choices, list) or not choices:
        return ""
    first = choices[0]
    if not isinstance(first, dict):
        return ""
    message = first.get("message")
    if not isinstance(message, dict):
        return ""
    content = message.get("content")
    return content.strip() if isinstance(content, str) else ""


def request_chat_reply(text: str, *, device_id: str, language: str = "auto") -> str:
    """调用火山方舟在线推理并返回文本回复。

    本轮只做单轮对话，不保存长期上下文。system prompt 要求回复简洁自然，
    是为了 V0.8 把文本交给 TTS 时更适合语音播放。
    """

    settings = get_settings()
    if not settings.llm_configured:
        raise LlmConfigMissingError("LLM Config Missing")

    trimmed = text.strip()
    if len(trimmed) > settings.llm_max_input_chars:
        raise ValueError(f"text too long, max {settings.llm_max_input_chars} chars")

    body = {
        "model": settings.llm_model,
        "messages": [
            {
                "role": "system",
                "content": "你是 ESP32-S3 AI 语音终端的中文语音助手，回答要简洁、自然，适合后续 TTS 播放。",
            },
            {
                "role": "user",
                "content": trimmed,
            },
        ],
        "max_tokens": settings.llm_max_output_tokens,
        "temperature": 0.7,
    }
    data = json.dumps(body, ensure_ascii=False).encode("utf-8")
    request = urllib.request.Request(
        _build_chat_url(settings),
        data=data,
        method="POST",
        headers={
            "Authorization": f"Bearer {settings.ark_api_key.strip()}",
            "Content-Type": "application/json",
        },
    )

    logger.info(
        "LLM request: device_id=%s provider=%s language=%s input_chars=%d max_tokens=%d",
        device_id,
        settings.llm_provider,
        language,
        len(trimmed),
        settings.llm_max_output_tokens,
    )

    try:
        with urllib.request.urlopen(request, timeout=settings.llm_timeout_seconds) as response:
            raw = response.read().decode("utf-8", errors="replace")
            payload = json.loads(raw)
    except urllib.error.HTTPError as exc:
        body_prefix = exc.read(300).decode("utf-8", errors="replace")
        logger.warning("LLM HTTP error: status=%s body=%s", exc.code, body_prefix)
        raise RuntimeError(f"LLM HTTP {exc.code}") from exc
    except Exception as exc:  # noqa: BLE001
        logger.warning("LLM request failed: %s", type(exc).__name__)
        raise RuntimeError(f"LLM request failed: {type(exc).__name__}") from exc

    reply = _extract_reply_text(payload)
    if not reply:
        raise RuntimeError("LLM empty reply")

    # 可以记录回复文本用于服务器侧调试，但绝不记录 ARK_API_KEY。
    logger.info("LLM reply: device_id=%s text=%s", device_id, reply)
    return reply
