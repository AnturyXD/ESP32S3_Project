"""运行时审计接口。"""

from fastapi import APIRouter

from app.core.config import get_settings

router = APIRouter(prefix="/audit", tags=["audit"])


@router.get("/runtime")
def runtime() -> dict[str, object]:
    """返回非敏感运行配置摘要。

    该接口用于确认服务是否仍然绑定 127.0.0.1:18080，以及密钥是否已配置。
    它只返回布尔值，不返回任何密钥、Token、密码或数据库连接串。
    """

    return get_settings().runtime_audit_summary()
