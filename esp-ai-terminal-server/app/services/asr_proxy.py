"""火山 ASR 代理服务。

V0.6 开始，服务器承担 ESP32 与火山 ASR 之间的协议转换：
设备只需要连接本服务器 WebSocket 并发送 16kHz/16bit/mono PCM，真实
ASR_APP_KEY / ASR_ACCESS_KEY 只保存在服务器本地 .env 中。这样可以避免
把云端密钥烧录到 ESP32，也便于后续统一做限流、日志和 HTTPS 入口。
"""

from __future__ import annotations

import asyncio
import gzip
import json
import logging
import struct
import uuid
from dataclasses import dataclass
from typing import Any

import websockets

from app.core.config import Settings

logger = logging.getLogger(__name__)

# 火山大模型流式 ASR v1 二进制协议基础常量。
PROTOCOL_VERSION = 0x1
DEFAULT_HEADER_SIZE = 0x1
MESSAGE_TYPE_FULL_CLIENT_REQUEST = 0x1
MESSAGE_TYPE_AUDIO_ONLY_REQUEST = 0x2
MESSAGE_TYPE_FULL_SERVER_RESPONSE = 0x9
MESSAGE_TYPE_SERVER_ERROR_RESPONSE = 0xF
FLAGS_NO_SEQUENCE = 0x0
FLAGS_POS_SEQUENCE = 0x1
FLAGS_NEG_WITH_SEQUENCE = 0x3
SERIALIZATION_NONE = 0x0
SERIALIZATION_JSON = 0x1
COMPRESSION_NONE = 0x0
COMPRESSION_GZIP = 0x1


def get_asr_status() -> dict[str, object]:
    """返回 ASR 代理能力摘要，供占位 AI 接口展示。

    V0.6 已提供设备音频 ASR WebSocket，但仍不接 LLM/TTS，也不在服务器本地
    跑模型。该函数保留给早期 ai_proxy 路由，避免破坏既有占位接口。
    """

    return {
        "enabled": True,
        "provider": "volcengine",
        "websocket": "/ws/esp-ai-terminal/asr",
        "message": "V0.6 proxies ESP32 PCM audio to Volcengine ASR through server WebSocket",
    }


def _build_header(message_type: int, flags: int, serialization: int, compression: int) -> bytes:
    """构造火山 ASR 二进制协议 4 字节基础头。"""

    return bytes(
        [
            (PROTOCOL_VERSION << 4) | DEFAULT_HEADER_SIZE,
            (message_type << 4) | flags,
            (serialization << 4) | compression,
            0x00,
        ]
    )


def _pack_full_client_request(payload: dict[str, Any], sequence: int) -> bytes:
    """封装首帧 JSON 请求。

    首帧只携带音频格式、识别参数和 request_id，不包含任何密钥；密钥只放在
    WebSocket 握手 Header 中，避免被业务日志误打印。
    """

    raw_payload = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    compressed_payload = gzip.compress(raw_payload)
    header = _build_header(
        MESSAGE_TYPE_FULL_CLIENT_REQUEST,
        FLAGS_POS_SEQUENCE,
        SERIALIZATION_JSON,
        COMPRESSION_GZIP,
    )
    return header + struct.pack(">iI", sequence, len(compressed_payload)) + compressed_payload


def _pack_audio_request(audio_chunk: bytes, sequence: int, is_last: bool) -> bytes:
    """封装 PCM 音频帧。

    最后一帧必须使用带负序号的 flag=0x3。S0.5 实测如果只使用 0x2，火山服务端
    会把负序号当成 payload size 解析，导致 declared body size mismatch。
    """

    compressed_payload = gzip.compress(audio_chunk)
    flags = FLAGS_NEG_WITH_SEQUENCE if is_last else FLAGS_POS_SEQUENCE
    wire_sequence = -abs(sequence) if is_last else abs(sequence)
    header = _build_header(MESSAGE_TYPE_AUDIO_ONLY_REQUEST, flags, SERIALIZATION_NONE, COMPRESSION_GZIP)
    return header + struct.pack(">iI", wire_sequence, len(compressed_payload)) + compressed_payload


def _decode_response(frame: bytes) -> dict[str, Any]:
    """解析火山 ASR 返回帧，保留足够排查信息。"""

    if len(frame) < 4:
        return {"type": "invalid", "detail": f"frame too short: {len(frame)} bytes"}

    version = frame[0] >> 4
    header_size = frame[0] & 0x0F
    message_type = frame[1] >> 4
    flags = frame[1] & 0x0F
    serialization = frame[2] >> 4
    compression = frame[2] & 0x0F
    offset = header_size * 4

    result: dict[str, Any] = {
        "version": version,
        "message_type": message_type,
        "flags": flags,
        "serialization": serialization,
        "compression": compression,
    }

    try:
        if message_type == MESSAGE_TYPE_SERVER_ERROR_RESPONSE:
            error_code, payload_size = struct.unpack_from(">II", frame, offset)
            offset += 8
            result["error_code"] = error_code
        else:
            if flags in (FLAGS_POS_SEQUENCE, FLAGS_NEG_WITH_SEQUENCE):
                sequence = struct.unpack_from(">i", frame, offset)[0]
                offset += 4
                result["sequence"] = sequence
            payload_size = struct.unpack_from(">I", frame, offset)[0]
            offset += 4

        payload = frame[offset : offset + payload_size]
        if compression == COMPRESSION_GZIP and payload:
            payload = gzip.decompress(payload)
        if serialization == SERIALIZATION_JSON and payload:
            result["payload"] = json.loads(payload.decode("utf-8"))
        elif payload:
            result["payload_text"] = payload.decode("utf-8", errors="replace")
        else:
            result["payload"] = None
    except Exception as exc:  # noqa: BLE001
        result["parse_error"] = repr(exc)
        result["raw_hex_prefix"] = frame[:80].hex()

    return result


def _find_text(value: Any) -> str:
    """从火山返回的嵌套 JSON 中尽量提取识别文本。

    火山不同返回阶段可能把文本放在 result.text、utterances[*].text 等位置。
    这里采用保守递归提取：只读取常见文本字段，不把 log_id 等排查字段误当成文本。
    """

    text_keys = {"text", "utterance_text", "sentence"}
    if isinstance(value, dict):
        for key in text_keys:
            found = value.get(key)
            if isinstance(found, str) and found.strip():
                return found.strip()
        for child in value.values():
            found = _find_text(child)
            if found:
                return found
    elif isinstance(value, list):
        parts = [_find_text(item) for item in value]
        return "".join(part for part in parts if part)
    return ""


def _response_to_device_message(decoded: dict[str, Any]) -> dict[str, object] | None:
    """把火山响应转换成设备可理解的 partial/final/error 消息。"""

    if decoded.get("message_type") == MESSAGE_TYPE_SERVER_ERROR_RESPONSE:
        payload = decoded.get("payload") or decoded.get("payload_text") or ""
        return {
            "type": "error",
            "message": str(payload)[:240],
            "error_code": decoded.get("error_code"),
        }

    payload = decoded.get("payload")
    text = _find_text(payload)
    is_final = decoded.get("flags") == FLAGS_NEG_WITH_SEQUENCE or int(decoded.get("sequence") or 0) < 0

    # 初始化 ACK 往往只有 log_id，没有文本。没有文本且非 final 时不用转发给设备刷屏。
    if not text and not is_final:
        return None

    return {
        "type": "final" if is_final else "partial",
        "text": text,
    }


def _build_auth_headers(settings: Settings, request_id: str) -> dict[str, str]:
    """构造火山 ASR 握手 Header。

    当前项目已确认使用 ASR_APP_KEY + ASR_ACCESS_KEY，不再使用单 Key 模式。
    仍保留单 Key 兜底是为了兼容旧配置，但日志只打印模式，不打印密钥原文。
    """

    if settings.asr_app_access_key_configured:
        return {
            "X-Api-App-Key": settings.asr_app_key.strip(),
            "X-Api-Access-Key": settings.asr_access_key.strip(),
            "X-Api-Resource-Id": settings.asr_resource_id.strip(),
            "X-Api-Connect-Id": request_id,
        }

    return {
        "X-Api-Key": settings.asr_api_key.strip(),
        "X-Api-Resource-Id": settings.asr_resource_id.strip(),
        "X-Api-Request-Id": request_id,
        "X-Api-Sequence": "1",
    }


async def _connect_with_compatible_headers(url: str, headers: dict[str, str]):
    """兼容 websockets 10.x 和 15.x 的 Header 参数名。"""

    try:
        return await websockets.connect(url, extra_headers=headers, ping_interval=None)
    except TypeError:
        return await websockets.connect(url, additional_headers=headers, ping_interval=None)


@dataclass
class AsrStartParams:
    """设备发起 ASR 会话时声明的音频参数。"""

    device_id: str
    sample_rate: int
    bits: int
    channels: int
    audio_format: str


class VolcengineAsrSession:
    """单次火山 ASR WebSocket 会话。

    一个 ESP32 Start ASR 操作对应一个对象。对象内部维护火山协议序号和已发送
    音频秒数，便于限制单次识别时长，避免设备异常导致持续计费。
    """

    def __init__(self, settings: Settings, start: AsrStartParams) -> None:
        self.settings = settings
        self.start = start
        self.request_id = str(uuid.uuid4())
        self.sequence = 1
        self.websocket = None
        self.sent_audio_bytes = 0
        self.x_tt_logid = "N/A"

    @property
    def sent_seconds(self) -> float:
        bytes_per_second = self.start.sample_rate * self.start.channels * (self.start.bits // 8)
        if bytes_per_second <= 0:
            return 0.0
        return self.sent_audio_bytes / bytes_per_second

    async def connect(self) -> None:
        """连接火山 ASR 并发送首帧配置。"""

        if not self.settings.asr_configured:
            raise RuntimeError("ASR Config Missing")

        headers = _build_auth_headers(self.settings, self.request_id)
        self.websocket = await _connect_with_compatible_headers(self.settings.asr_ws_url, headers)

        response_headers = getattr(self.websocket, "response_headers", None) or getattr(
            getattr(self.websocket, "response", None), "headers", {}
        )
        if response_headers:
            self.x_tt_logid = response_headers.get("X-Tt-Logid") or response_headers.get("x-tt-logid") or "N/A"

        logger.info(
            "volcengine ASR connected: device_id=%s request_id=%s x_tt_logid=%s auth_mode=%s resource_id=%s",
            self.start.device_id,
            self.request_id,
            self.x_tt_logid,
            self.settings.asr_auth_mode,
            self.settings.asr_resource_id,
        )

        payload = {
            "user": {"uid": self.start.device_id},
            "audio": {
                "format": self.start.audio_format,
                "sample_rate": self.start.sample_rate,
                "bits": self.start.bits,
                "channel": self.start.channels,
                "codec": "raw",
            },
            "request": {
                "model_name": "bigmodel",
                "enable_itn": True,
                "enable_punc": True,
                "show_utterances": True,
                "request_id": self.request_id,
            },
        }
        await self.websocket.send(_pack_full_client_request(payload, sequence=self.sequence))

    async def send_audio(self, pcm: bytes, *, is_last: bool) -> None:
        """发送一帧 PCM 到火山 ASR。"""

        if self.websocket is None:
            raise RuntimeError("ASR websocket is not connected")
        self.sequence += 1
        await self.websocket.send(_pack_audio_request(pcm, sequence=self.sequence, is_last=is_last))
        self.sent_audio_bytes += len(pcm)

    async def recv_available(self, timeout_s: float) -> list[dict[str, object]]:
        """读取当前可用的火山响应，超时则返回空列表。"""

        if self.websocket is None:
            return []
        messages: list[dict[str, object]] = []
        while True:
            try:
                frame = await asyncio.wait_for(self.websocket.recv(), timeout=timeout_s)
            except asyncio.TimeoutError:
                break
            except Exception as exc:  # noqa: BLE001
                # 火山 ASR 在收到最后一帧后可能以 1000/OK 正常关闭连接。
                # 这表示本次识别结束，不应该被当作代理错误返回给设备。
                if type(exc).__name__ == "ConnectionClosedOK":
                    logger.info("volcengine ASR closed normally: %s", exc)
                    break
                raise
            if isinstance(frame, str):
                messages.append({"type": "partial", "text": frame})
            else:
                decoded = _decode_response(frame)
                logger.debug("volcengine ASR frame: %s", decoded)
                device_message = _response_to_device_message(decoded)
                if device_message is not None:
                    messages.append(device_message)
            timeout_s = 0.01
        return messages

    async def close(self) -> None:
        """关闭火山 ASR 连接。"""

        if self.websocket is not None:
            await self.websocket.close()
            self.websocket = None
