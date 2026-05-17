"""版本信息接口。"""

from fastapi import APIRouter

from app.core.config import get_settings

router = APIRouter(tags=["version"])


@router.get("/version")
def version() -> dict[str, str]:
    """返回服务版本和阶段，便于 ESP32 端和人工调试确认服务版本。"""

    settings = get_settings()
    return {
        "service": settings.app_name,
        "version": settings.app_version,
        "stage": settings.app_stage,
    }
