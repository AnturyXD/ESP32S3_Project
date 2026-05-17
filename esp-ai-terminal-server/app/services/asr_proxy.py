"""ASR 代理占位服务。

后续 V0.6 会在这里接入云端 ASR。当前阶段禁止访问真实第三方 API，
也不要求用户提供任何 API Key。
"""


def get_asr_status() -> dict[str, object]:
    """返回 ASR 占位状态。"""

    return {"enabled": False, "provider": "placeholder", "message": "ASR proxy is reserved for V0.6"}
