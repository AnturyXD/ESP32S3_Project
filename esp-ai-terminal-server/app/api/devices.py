"""设备注册、心跳和状态查询 API。

所有接口都挂在 /api/esp-ai-terminal/devices 下，便于和服务器上其他项目区分。
当前 S0.4 只使用内存字典保存状态，不接数据库、不写文件、不记录敏感密钥。
"""

from fastapi import APIRouter, Depends

from app.core.security import require_device_auth
from app.schemas.device import DeviceHeartbeatRequest, DeviceRegisterRequest
from app.services.device_registry import registry

router = APIRouter(
    prefix="/api/esp-ai-terminal/devices",
    tags=["devices"],
    dependencies=[Depends(require_device_auth)],
)


@router.post("/register")
def register_device(payload: DeviceRegisterRequest) -> dict[str, object]:
    """登记设备。

    该接口用于验证 ESP32 能把设备 ID、固件版本、硬件型号和 IP 发到服务器。
    S0.4 使用 X-Device-Token 做最小共享密钥校验，真实 token 签发和持久化
    存储后续再做。
    """

    return registry.register(payload)


@router.post("/heartbeat")
def heartbeat(payload: DeviceHeartbeatRequest) -> dict[str, object]:
    """接收设备心跳。

    心跳用于上报 Wi-Fi、Storage、Audio、Power 等状态。当前只保存到内存，
    服务重启会丢失，符合 S0.4 最小链路验证目标。
    """

    return registry.heartbeat(payload)


@router.get("/{device_id}/status")
def get_device_status(device_id: str) -> dict[str, object]:
    """查询设备最近状态。

    返回同一份内存状态源，方便 curl 验证 register -> heartbeat -> status 的链路。
    该接口同样要求设备 token，避免公网临时调试时泄露设备状态。
    """

    return registry.get_status(device_id)
