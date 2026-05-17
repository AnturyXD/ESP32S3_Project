"""ASR 代理占位服务。

S0.5 只允许服务器侧 smoke test 验证火山 ASR 配置与 WebSocket 协议，
还不接 ESP32 音频上传，也不提供正式 ASR 代理接口。真正的设备音频转发
会放到 V0.6，避免一次性把设备端、服务器端和云端链路全堆在一起。
"""


def get_asr_status() -> dict[str, object]:
    """返回 ASR 占位状态。"""

    return {
        "enabled": False,
        "provider": "volcengine",
        "message": "S0.5 only provides server-side ASR config check and smoke test; proxy is reserved for V0.6",
    }
