#!/usr/bin/env python3
"""火山 ASR 服务器侧 smoke test。

本脚本只在服务器本地运行，用于验证：
1. `.env` 中的火山 ASR 配置是否完整；
2. WebSocket 是否能连接到火山 ASR；
3. API Key / Resource ID / 二进制封包格式是否基本可用。

它不会读取 ESP32 音频，也不会启动任何服务。真实 ASR_API_KEY 只从服务器
本地 `.env` 读取，日志中只打印“是否配置”，绝不打印 Key 原文。
"""

from __future__ import annotations

import asyncio
import gzip
import json
import math
import os
import struct
import sys
import uuid
from pathlib import Path
from typing import Any

from dotenv import load_dotenv

# 让脚本无论从 server 根目录还是 scripts 目录执行，都能导入 app 包。
SERVER_ROOT = Path(__file__).resolve().parents[1]
if str(SERVER_ROOT) not in sys.path:
    sys.path.insert(0, str(SERVER_ROOT))

from app.core.config import get_settings  # noqa: E402


# 火山 ASR WebSocket 二进制协议的 4 字节基础头。
# 这里按官方 Demo 常见格式组织：version/header_size、message_type/flags、
# serialization/compression、reserved。后续 payload 根据消息类型追加。
PROTOCOL_VERSION = 0x1
DEFAULT_HEADER_SIZE = 0x1

MESSAGE_TYPE_FULL_CLIENT_REQUEST = 0x1
MESSAGE_TYPE_AUDIO_ONLY_REQUEST = 0x2
MESSAGE_TYPE_FULL_SERVER_RESPONSE = 0x9
MESSAGE_TYPE_SERVER_ACK = 0xB
MESSAGE_TYPE_SERVER_ERROR_RESPONSE = 0xF

MESSAGE_TYPE_SPECIFIC_FLAGS_NO_SEQUENCE = 0x0
MESSAGE_TYPE_SPECIFIC_FLAGS_POS_SEQUENCE = 0x1
MESSAGE_TYPE_SPECIFIC_FLAGS_NEG_SEQUENCE = 0x2
MESSAGE_TYPE_SPECIFIC_FLAGS_NEG_WITH_SEQUENCE = 0x3

SERIALIZATION_NONE = 0x0
SERIALIZATION_JSON = 0x1

COMPRESSION_NONE = 0x0
COMPRESSION_GZIP = 0x1


def _build_header(
    message_type: int,
    flags: int,
    serialization: int,
    compression: int,
) -> bytes:
    """构造火山 ASR 二进制协议基础头。

    smoke test 必须使用二进制协议，不能用普通文本 WebSocket 冒充成功。
    """

    return bytes(
        [
            (PROTOCOL_VERSION << 4) | DEFAULT_HEADER_SIZE,
            (message_type << 4) | flags,
            (serialization << 4) | compression,
            0x00,
        ]
    )


def _pack_full_client_request(payload: dict[str, Any], sequence: int) -> bytes:
    """封装客户端完整请求。

    第一帧携带音频格式、采样率等 JSON 元信息。payload 使用 gzip 压缩，
    是为了匹配火山 ASR 官方 Demo 的二进制封包方向。
    """

    raw_payload = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    compressed_payload = gzip.compress(raw_payload)
    header = _build_header(
        MESSAGE_TYPE_FULL_CLIENT_REQUEST,
        MESSAGE_TYPE_SPECIFIC_FLAGS_POS_SEQUENCE,
        SERIALIZATION_JSON,
        COMPRESSION_GZIP,
    )
    return header + struct.pack(">iI", sequence, len(compressed_payload)) + compressed_payload


def _pack_audio_request(audio_chunk: bytes, sequence: int, is_last: bool) -> bytes:
    """封装音频分包。

    火山协议用负序号表示最后一个音频包。这里每 200ms 发送一次 PCM，
    最后一包使用负序号，便于服务端知道音频流已经结束。
    """

    compressed_payload = gzip.compress(audio_chunk)
    # 最后一包必须使用 “negative sequence with sequence number”(0x3)。
    # 如果只用 0x2，火山服务端会按“没有序号”的格式解析，进而把 int32 负序号
    # 当成 payload size，报 declared body size does not match actual body size。
    flags = (
        MESSAGE_TYPE_SPECIFIC_FLAGS_NEG_WITH_SEQUENCE
        if is_last
        else MESSAGE_TYPE_SPECIFIC_FLAGS_POS_SEQUENCE
    )
    wire_sequence = -abs(sequence) if is_last else abs(sequence)
    header = _build_header(
        MESSAGE_TYPE_AUDIO_ONLY_REQUEST,
        flags,
        SERIALIZATION_NONE,
        COMPRESSION_GZIP,
    )
    return header + struct.pack(">iI", wire_sequence, len(compressed_payload)) + compressed_payload


def _decode_response(frame: bytes) -> dict[str, Any]:
    """尽量解析火山 ASR 返回帧，失败时返回十六进制摘要。

    不同错误场景下服务端可能返回 JSON、压缩 JSON 或错误字符串。smoke test
    的目标是让排查信息足够清晰，而不是吞掉异常。
    """

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
            if flags in (
                MESSAGE_TYPE_SPECIFIC_FLAGS_POS_SEQUENCE,
                MESSAGE_TYPE_SPECIFIC_FLAGS_NEG_SEQUENCE,
                MESSAGE_TYPE_SPECIFIC_FLAGS_NEG_WITH_SEQUENCE,
            ):
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


def _generate_pcm_sine(
    seconds: float,
    sample_rate: int,
    channels: int,
    frequency: float = 440.0,
) -> bytes:
    """生成 16kHz/16bit/mono PCM 测试音。

    当前 smoke test 只验证服务端到火山 ASR 的链路，不要求识别出有意义文字。
    使用测试音比静音更容易观察“已发送音频秒数”和服务端响应，但识别结果为空
    也不代表链路失败。
    """

    if channels != 1:
        raise ValueError("S0.5 smoke test 仅生成 mono PCM")

    sample_count = int(seconds * sample_rate)
    amplitude = 0.18 * 32767
    pcm = bytearray()
    for index in range(sample_count):
        value = int(amplitude * math.sin(2 * math.pi * frequency * index / sample_rate))
        pcm.extend(struct.pack("<h", value))
    return bytes(pcm)


def _make_request_payload(settings: Any, request_id: str) -> dict[str, Any]:
    """构造 ASR 元信息。

    字段保持显式，方便后续根据火山文档调整。这里不包含任何密钥，密钥只放在
    WebSocket 握手 Header 中。
    """

    return {
        "user": {"uid": "esp-ai-terminal-smoke-test"},
        "audio": {
            "format": settings.asr_audio_format,
            "sample_rate": settings.asr_sample_rate,
            "bits": settings.asr_bits,
            "channel": settings.asr_channels,
            "codec": "raw",
        },
        "request": {
            "model_name": "bigmodel",
            "enable_itn": True,
            "enable_punc": True,
            "show_utterances": True,
            "request_id": request_id,
        },
    }


def _build_auth_headers(settings: Any, request_id: str) -> dict[str, str]:
    """构造 WebSocket 握手鉴权 Header。

    用户当前规划里使用 `X-Api-Key`；火山大模型流式 ASR 官方文档/示例中也常见
    `X-Api-App-Key` + `X-Api-Access-Key`。为了降低凭证体系差异带来的阻塞：
    1. 如果 `.env` 同时配置了 ASR_APP_KEY / ASR_ACCESS_KEY，优先使用官方流式 Header；
    2. 否则使用任务要求的 ASR_API_KEY -> X-Api-Key；
    3. 两种模式都不会打印密钥原文。
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
        # 按用户指定和火山接口约定提供握手序号；二进制包内仍使用协议序号。
        "X-Api-Sequence": "1",
    }


async def _connect_with_compatible_headers(url: str, headers: dict[str, str]):
    """?? websockets 10.x ? 15.x ??????????"""

    import websockets

    try:
        return await websockets.connect(url, extra_headers=headers, ping_interval=None)
    except TypeError:
        return await websockets.connect(url, additional_headers=headers, ping_interval=None)


def _format_header_names(headers: dict[str, str]) -> str:
    """??? Header ????????????"""

    return ",".join(sorted(headers.keys()))


def _get_header_case_insensitive(headers: Any, name: str) -> str | None:
    """??? websockets ??????????????????"""

    if not headers:
        return None
    try:
        value = headers.get(name)
        if value:
            return str(value)
    except Exception:  # noqa: BLE001
        pass

    lower_name = name.lower()
    try:
        for key, value in headers.items():
            if str(key).lower() == lower_name:
                return str(value)
    except Exception:  # noqa: BLE001
        return None
    return None


def _print_handshake_failure(exc: Exception, headers: dict[str, str]) -> None:
    """?? WebSocket ?????????????

    ?? ASR ? 401/403 ???????????????????????
    HTTP ????????? X-Tt-Logid ????????????????
    ????? Header ????????? Header ??????????????
    """

    response = getattr(exc, "response", None)
    status_code = getattr(response, "status_code", None) or getattr(exc, "status_code", None)
    reason = getattr(response, "reason_phrase", None) or getattr(exc, "reason", None)
    response_headers = getattr(response, "headers", None) or getattr(exc, "headers", None)
    response_body = getattr(response, "body", None)

    print(f"handshake_http_status={status_code or 'unknown'} reason={reason or 'N/A'}")
    print(f"request_header_names={_format_header_names(headers)}")

    if response_headers:
        interesting_headers = [
            "X-Tt-Logid",
            "x-tt-logid",
            "X-Api-Connect-Id",
            "Content-Type",
            "Date",
            "Server",
        ]
        for header_name in interesting_headers:
            value = _get_header_case_insensitive(response_headers, header_name)
            if value:
                print(f"response_header_{header_name}={value}")

    if response_body:
        if isinstance(response_body, bytes):
            body_text = response_body.decode("utf-8", errors="replace")
        else:
            body_text = str(response_body)
        print("response_body_prefix=" + body_text[:1000])


def _is_normal_asr_close(exc: Exception) -> bool:
    """判断火山 ASR 是否已经正常结束本次识别。

    火山在收到最后一个负序号音频包后，可能会先返回最终响应，再以
    WebSocket 1000/OK 关闭连接，并给出 `finish last sequence` 原因。
    这不是失败，而是“最后一包处理完成”。smoke test 需要把它视为成功，
    否则会把已经跑通的链路误报成错误。
    """

    exc_name = type(exc).__name__
    exc_text = str(exc).lower()
    return exc_name == "ConnectionClosedOK" and (
        "finish last sequence" in exc_text or "received 1000" in exc_text
    )


async def run_smoke_test() -> int:
    """执行一次火山 ASR smoke test。

    返回 0 表示脚本流程正常完成；如果配置缺失、连接失败或协议异常，则返回非 0。
    """

    load_dotenv(SERVER_ROOT / ".env", override=True)
    get_settings.cache_clear()
    settings = get_settings()

    print("ASR smoke test start")
    print(f"server_root={SERVER_ROOT}")
    print(f"provider={settings.asr_provider}")
    print(f"ws_url={settings.asr_ws_url}")
    print(f"resource_id={settings.asr_resource_id}")
    print(f"api_key_configured={settings.asr_api_key_configured}")
    print(f"app_key_configured={bool(settings.asr_app_key.strip())}")
    print(f"access_key_configured={bool(settings.asr_access_key.strip())}")
    print(f"auth_mode={settings.asr_auth_mode}")
    print(
        "audio="
        f"{settings.asr_sample_rate}Hz/{settings.asr_bits}bit/"
        f"{settings.asr_channels}ch packet_ms={settings.asr_packet_ms}"
    )

    if not settings.asr_configured:
        print("ERROR: Config Missing. 请先在服务器本地 .env 填写 ASR_API_KEY、ASR_WS_URL 和 ASR_RESOURCE_ID。")
        return 2

    if settings.asr_bits != 16 or settings.asr_channels != 1:
        print("ERROR: S0.5 smoke test 当前固定验证 16bit / mono PCM。")
        return 2

    request_id = str(uuid.uuid4())
    headers = _build_auth_headers(settings, request_id)

    pcm = _generate_pcm_sine(
        seconds=2.0,
        sample_rate=settings.asr_sample_rate,
        channels=settings.asr_channels,
    )
    bytes_per_ms = settings.asr_sample_rate * settings.asr_channels * (settings.asr_bits // 8) / 1000
    chunk_size = int(bytes_per_ms * settings.asr_packet_ms)
    chunks = [pcm[index : index + chunk_size] for index in range(0, len(pcm), chunk_size)]

    print(f"request_id={request_id}")
    print(f"generated_pcm_bytes={len(pcm)} chunks={len(chunks)}")
    print("estimated_billable_audio_seconds=2.0")

    try:
        ws = await _connect_with_compatible_headers(settings.asr_ws_url, headers)
        async with ws:
            response_headers = getattr(ws, "response_headers", None) or getattr(
                getattr(ws, "response", None), "headers", {}
            )
            logid = None
            if response_headers:
                logid = response_headers.get("X-Tt-Logid") or response_headers.get("x-tt-logid")
            print(f"websocket_connected=true x_tt_logid={logid or 'N/A'}")

            init_payload = _make_request_payload(settings, request_id)
            await ws.send(_pack_full_client_request(init_payload, sequence=1))
            print("sent_init_request=true")

            for index, chunk in enumerate(chunks, start=2):
                is_last = index == len(chunks) + 1
                await ws.send(_pack_audio_request(chunk, sequence=index, is_last=is_last))
                sent_seconds = min(2.0, (index - 1) * settings.asr_packet_ms / 1000)
                print(f"sent_audio_chunk={index - 1}/{len(chunks)} seconds={sent_seconds:.1f}")

                # 尽量读取服务端响应，但不给无限等待。ASR 可能在最后几包后才返回结果。
                try:
                    frame = await asyncio.wait_for(ws.recv(), timeout=1.5)
                    if isinstance(frame, str):
                        print(f"server_text_message={frame}")
                    else:
                        decoded = _decode_response(frame)
                        print("server_binary_message=" + json.dumps(decoded, ensure_ascii=False))
                except asyncio.TimeoutError:
                    print("server_response_timeout=1.5s; continue")

            # 最后一包后再等待一次最终响应，方便看到错误码或最终识别结果。
            try:
                frame = await asyncio.wait_for(ws.recv(), timeout=5.0)
                if isinstance(frame, str):
                    print(f"server_final_text_message={frame}")
                else:
                    decoded = _decode_response(frame)
                    print("server_final_binary_message=" + json.dumps(decoded, ensure_ascii=False))
            except asyncio.TimeoutError:
                print("server_final_response_timeout=5s")
            except Exception as exc:  # noqa: BLE001
                if _is_normal_asr_close(exc):
                    print("server_closed_normally=true reason=finish last sequence")
                else:
                    raise

    except Exception as exc:  # noqa: BLE001
        if _is_normal_asr_close(exc):
            print("server_closed_normally=true reason=finish last sequence")
            print("ASR smoke test finished successfully.")
            return 0
        print(f"ERROR: ASR smoke test failed: {type(exc).__name__}: {exc}")
        _print_handshake_failure(exc, headers)
        print("排查建议：检查 ASR_API_KEY、ASR_RESOURCE_ID、服务器出站网络、WebSocket 地址和协议封包。")
        return 1

    print("ASR smoke test finished successfully. 若返回为空文本但没有鉴权/协议错误，说明链路已通，测试音本身不可识别。")
    return 0


def main() -> None:
    """脚本入口。"""

    raise SystemExit(asyncio.run(run_smoke_test()))


if __name__ == "__main__":
    main()
