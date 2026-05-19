"""LLM 配置检查接口。

该接口只用于部署和排障，返回火山方舟配置是否完整，不返回
ARK_API_KEY、模型接入点原文或任何敏感密钥。
"""

from fastapi import APIRouter

from app.core.config import get_settings

router = APIRouter(prefix="/api/esp-ai-terminal/llm", tags=["llm"])


@router.get("/config")
def get_llm_config() -> dict[str, object]:
    """返回 LLM 非敏感配置摘要。"""

    return get_settings().llm_public_config_summary()
