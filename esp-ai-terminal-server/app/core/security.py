"""设备接口的最小鉴权依赖。

S0.4 只实现临时共享密钥方案：ESP32 在请求头 `X-Device-Token`
中携带设备共享密钥，服务端与本地环境变量 `DEVICE_SHARED_SECRET`
做常量时间比较。这个方案足够验证设备到服务器的链路，但不适合长期
承载正式产品，后续可以升级为设备证书、JWT 或短期 token。
"""

import logging
import secrets

from fastapi import Header, HTTPException, status

from app.core.config import get_settings

logger = logging.getLogger(__name__)
_warned_missing_secret = False


def is_device_token_valid(x_device_token: str | None) -> bool:
    """判断设备 token 是否有效，供 HTTP 依赖和 WebSocket 握手共用。

    WebSocket 不能直接复用普通 HTTP Header 依赖，因此把核心比较逻辑抽出来。
    如果 DEVICE_SHARED_SECRET 未配置，仍按开发阶段策略临时放行，但只打印一次 warning。
    """

    global _warned_missing_secret

    settings = get_settings()
    if not settings.device_auth_configured:
        if not _warned_missing_secret:
            logger.warning(
                "DEVICE_SHARED_SECRET is not configured; device API auth is temporarily bypassed"
            )
            _warned_missing_secret = True
        return True

    expected = settings.device_shared_secret.strip()
    received = (x_device_token or "").strip()
    return bool(received) and secrets.compare_digest(received, expected)


def require_device_auth(x_device_token: str | None = Header(default=None)) -> None:
    """校验设备请求头中的共享密钥。

    开发阶段如果 `DEVICE_SHARED_SECRET` 为空或仍为 `CHANGE_ME`，允许请求通过，
    但只打印一次 warning，提醒部署者这不是安全生产配置。日志里绝不打印
    token 原文，避免终端截图或日志文件泄露密钥。
    """

    if not is_device_token_valid(x_device_token):
        logger.warning("device auth failed: token missing or mismatch")
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="invalid device token",
        )
