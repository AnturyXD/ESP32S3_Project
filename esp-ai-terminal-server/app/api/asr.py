"""服务器侧 ASR 配置检查接口。

S0.5 只接入“服务器侧火山 ASR 自测”能力，不接 ESP32 音频上传，也不做
端到端实时语音交互。该接口只用于确认服务器本地 `.env` 是否已经提供
ASR smoke test 所需的非敏感参数。
"""

from fastapi import APIRouter

from app.core.config import get_settings

router = APIRouter(prefix="/api/esp-ai-terminal/asr", tags=["asr"])


@router.get("/config")
def get_asr_config() -> dict[str, object]:
    """返回 ASR 配置摘要。

    为了保护火山 ASR_API_KEY，本接口只返回 `api_key_configured` 布尔值。
    即使后续临时公网暴露 18080，也不能通过该接口拿到 Key、Token 或密码。
    """

    return get_settings().asr_public_config_summary()
