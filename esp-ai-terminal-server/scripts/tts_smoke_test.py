#!/usr/bin/env python3
"""火山 TTS 服务器侧 smoke test。

脚本只在服务器本地运行，用于验证 `.env` 中的 TTS 配置、鉴权、接口地址
和返回音频格式。它不会启动服务，也不会打印任何 TTS 密钥原文。
"""

from __future__ import annotations

import sys
from pathlib import Path

from dotenv import load_dotenv

SERVER_ROOT = Path(__file__).resolve().parents[1]
if str(SERVER_ROOT) not in sys.path:
    sys.path.insert(0, str(SERVER_ROOT))

from app.core.config import get_settings  # noqa: E402
from app.services.tts_proxy import (  # noqa: E402
    TtsConfigMissingError,
    TtsUnsupportedFormatError,
    synthesize_tts,
)


def main() -> int:
    """执行一次短文本 TTS 合成并保存到 tmp。"""

    load_dotenv(SERVER_ROOT / ".env", override=True)
    get_settings.cache_clear()
    settings = get_settings()

    print("TTS smoke test start")
    print(f"server_root={SERVER_ROOT}")
    print(f"provider={settings.tts_provider}")
    print(f"model={settings.tts_model}")
    print(f"api_version={settings.tts_api_version}")
    print(f"endpoint={settings.tts_effective_url}")
    print(f"resource_id={settings.tts_resource_id}")
    print(f"auth_mode={settings.tts_auth_mode}")
    print(f"credential_configured={settings.tts_credential_configured}")
    print(f"voice_configured={bool(settings.tts_voice_type.strip())}")
    print(
        "audio="
        f"{settings.tts_sample_rate}Hz/{settings.tts_bits}bit/"
        f"{settings.tts_channels}ch format={settings.tts_audio_format}"
    )

    try:
        result = synthesize_tts(
            "你好，我是ESP32-S3 AI语音终端。",
            device_id="esp32s3-tts-smoke-test",
            audio_format=settings.tts_audio_format,
            sample_rate=settings.tts_sample_rate,
        )
    except TtsConfigMissingError:
        print("ERROR: TTS Config Missing. 请在服务器本地 .env 填写 TTS Key/Token 和音色。")
        return 2
    except TtsUnsupportedFormatError as exc:
        print(f"ERROR: Unsupported audio format: {exc}")
        return 3
    except Exception as exc:  # noqa: BLE001
        print(f"ERROR: TTS smoke test failed: {type(exc).__name__}: {exc}")
        print("排查建议：检查 TTS_API_KEY 或 TTS_APP_ID/TTS_ACCESS_TOKEN、Resource ID、Voice Type、接口地址和账号服务开通状态。")
        return 1

    out_dir = SERVER_ROOT / "tmp"
    out_dir.mkdir(exist_ok=True)
    suffix = "wav" if result.audio_format == "wav" else "pcm"
    out_path = out_dir / f"tts_smoke_test.{suffix}"
    out_path.write_bytes(result.audio)

    bytes_per_second = result.sample_rate * result.channels * (result.bits // 8)
    estimated_seconds = len(result.audio) / bytes_per_second if bytes_per_second else 0.0
    print("TTS smoke test finished successfully")
    print(f"audio_format={result.audio_format}")
    print(f"sample_rate={result.sample_rate}")
    print(f"bits={result.bits}")
    print(f"channels={result.channels}")
    print(f"audio_bytes={len(result.audio)}")
    print(f"estimated_seconds={estimated_seconds:.2f}")
    print(f"output={out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
