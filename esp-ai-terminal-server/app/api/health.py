"""健康检查接口。"""

from fastapi import APIRouter

from app.core.config import get_settings

router = APIRouter(tags=["health"])


@router.get("/health")
def health() -> dict[str, str]:
    """返回最小健康状态。

    该接口不访问数据库、不访问第三方 API，只证明 FastAPI 进程本身可响应。
    """

    settings = get_settings()
    return {"status": "ok", "service": settings.app_name}
