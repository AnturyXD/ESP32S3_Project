"""LLM 代理占位服务。

服务器后续会保护真实 LLM API Key，ESP32 设备不应直接持有云端密钥。
"""


def get_llm_status() -> dict[str, object]:
    """返回 LLM 占位状态。"""

    return {"enabled": False, "provider": "placeholder", "message": "LLM proxy is reserved for V0.7"}
