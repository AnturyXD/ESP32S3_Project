"""服务器侧 ASR 接口。

包含两个能力：
1. `/api/esp-ai-terminal/asr/config`：只读配置检查，不返回密钥。
2. `/ws/esp-ai-terminal/asr`：V0.6 设备音频上传入口，把 ESP32 PCM 转发到火山 ASR。
"""

from __future__ import annotations

import json
import logging
from typing import Any

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from app.core.config import get_settings
from app.core.security import is_device_token_valid
from app.services.asr_proxy import AsrStartParams, VolcengineAsrSession

router = APIRouter(tags=["asr"])
logger = logging.getLogger(__name__)

MAX_ASR_SECONDS = 30.0
EXPECTED_SAMPLE_RATE = 16000
EXPECTED_BITS = 16
EXPECTED_CHANNELS = 1
EXPECTED_FORMAT = "pcm"


@router.get("/api/esp-ai-terminal/asr/config")
def get_asr_config() -> dict[str, object]:
    """返回 ASR 配置摘要。

    该接口只返回 Key 是否配置，禁止返回 ASR_APP_KEY、ASR_ACCESS_KEY、ASR_API_KEY
    原文或截断片段，避免临时公网调试时泄露云端凭证。
    """

    return get_settings().asr_public_config_summary()


def _parse_start_message(raw: str) -> AsrStartParams:
    """解析设备 start JSON 并做基础音频参数校验。"""

    try:
        payload: dict[str, Any] = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise ValueError("invalid start json") from exc

    if payload.get("type") != "start":
        raise ValueError("first message must be start")

    device_id = str(payload.get("device_id") or "").strip()
    sample_rate = int(payload.get("sample_rate") or 0)
    bits = int(payload.get("bits") or 0)
    channels = int(payload.get("channels") or 0)
    audio_format = str(payload.get("format") or "").strip().lower()

    if not device_id:
        raise ValueError("device_id is required")
    if sample_rate != EXPECTED_SAMPLE_RATE or bits != EXPECTED_BITS or channels != EXPECTED_CHANNELS:
        raise ValueError("audio format must be 16000Hz/16bit/mono")
    if audio_format != EXPECTED_FORMAT:
        raise ValueError("audio format must be pcm")

    return AsrStartParams(
        device_id=device_id,
        sample_rate=sample_rate,
        bits=bits,
        channels=channels,
        audio_format=audio_format,
    )


async def _send_json(websocket: WebSocket, payload: dict[str, object]) -> None:
    """向设备发送 JSON 消息。"""

    await websocket.send_text(json.dumps(payload, ensure_ascii=False))


@router.websocket("/ws/esp-ai-terminal/asr")
async def websocket_asr_proxy(websocket: WebSocket) -> None:
    """ESP32 设备侧 ASR WebSocket 入口。

    设备协议：先发 start JSON，再连续发送 binary PCM，最后发 stop JSON。
    服务器协议：把 PCM 转成火山 ASR 二进制协议，并把 partial/final/error JSON
    返回给设备。本接口不保存音频，除非后续单独加入 debug 开关。
    """

    settings = get_settings()
    token = websocket.headers.get("x-device-token")
    if not is_device_token_valid(token):
        await websocket.close(code=1008, reason="invalid device token")
        logger.warning("ASR websocket auth failed: token missing or mismatch")
        return

    await websocket.accept()

    session: VolcengineAsrSession | None = None
    pending_chunk: bytes | None = None
    stopped = False

    try:
        first = await websocket.receive_text()
        start = _parse_start_message(first)
        session = VolcengineAsrSession(settings, start)
        await session.connect()
        await _send_json(websocket, {"type": "started", "request_id": session.request_id})

        logger.info("ASR proxy started: device_id=%s", start.device_id)

        while True:
            message = await websocket.receive()

            if "text" in message and message["text"] is not None:
                payload = json.loads(message["text"])
                if payload.get("type") == "stop":
                    stopped = True
                    break
                await _send_json(websocket, {"type": "error", "message": "unknown text message"})
                continue

            pcm = message.get("bytes")
            if pcm is None:
                continue

            # 延迟一帧发送：这样 stop 到来时，可以把最后一帧标记为火山协议的 last packet。
            if pending_chunk is not None:
                await session.send_audio(pending_chunk, is_last=False)
                for response in await session.recv_available(timeout_s=0.01):
                    await _send_json(websocket, response)
            pending_chunk = pcm

            if session.sent_seconds >= MAX_ASR_SECONDS:
                await _send_json(websocket, {"type": "error", "message": "ASR max duration reached"})
                stopped = True
                break

        if session is not None:
            await session.send_audio(pending_chunk or b"", is_last=True)
            for response in await session.recv_available(timeout_s=5.0):
                await _send_json(websocket, response)
            await _send_json(
                websocket,
                {
                    "type": "final",
                    "text": "",
                    "sent_seconds": round(session.sent_seconds, 2),
                    "stopped": stopped,
                },
            )
            logger.info(
                "ASR proxy finished: device_id=%s sent_seconds=%.2f x_tt_logid=%s",
                start.device_id,
                session.sent_seconds,
                session.x_tt_logid,
            )

    except WebSocketDisconnect:
        logger.info("ASR websocket disconnected by device")
    except Exception as exc:  # noqa: BLE001
        logger.exception("ASR proxy failed: %s", type(exc).__name__)
        try:
            await _send_json(websocket, {"type": "error", "message": str(exc)[:240]})
        except Exception:  # noqa: BLE001
            pass
    finally:
        if session is not None:
            try:
                await session.close()
            except Exception:  # noqa: BLE001
                pass
