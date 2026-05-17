"""TTS 代理占位服务。

后续 V0.8 会在这里接入云端 TTS 或 WebSocket 转发。当前只保留边界。
"""


def get_tts_status() -> dict[str, object]:
    """返回 TTS 占位状态。"""

    return {"enabled": False, "provider": "placeholder", "message": "TTS proxy is reserved for V0.8"}
