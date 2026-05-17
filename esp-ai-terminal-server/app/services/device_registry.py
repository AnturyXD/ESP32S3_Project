"""设备注册与心跳的 S0.4 内存状态服务。

这是 S0.4 的临时实现：设备状态只保存在当前 Python 进程内存中，
服务重启后会丢失。这样可以先验证设备注册、心跳和查询接口，
不引入数据库、不写生产存储，也不会影响服务器已有 Carshow 项目。
后续进入稳定阶段后，再评估 SQLite、文件落盘或其他持久化方案。
"""

from copy import deepcopy
from datetime import datetime, timezone
from threading import Lock

from app.schemas.device import DeviceHeartbeatRequest, DeviceRegisterRequest


class DeviceRegistry:
    """线程安全的极简设备内存注册表。"""

    def __init__(self) -> None:
        self._lock = Lock()
        self._devices: dict[str, dict[str, object]] = {}

    @staticmethod
    def _now_iso() -> str:
        """返回 UTC ISO 时间字符串，便于日志和接口跨时区排查。"""

        return datetime.now(timezone.utc).isoformat()

    def register(self, payload: DeviceRegisterRequest) -> dict[str, object]:
        """登记设备基础信息。

        本函数不生成真实 token，也不保存任何密钥；它只把设备 ID、固件、硬件、IP
        放入内存字典，方便后续 heartbeat/status 验证同一台设备的状态流转。
        """

        now = self._now_iso()
        with self._lock:
            current = self._devices.get(payload.device_id, {})
            current.update(
                {
                    "device_id": payload.device_id,
                    "registered": True,
                    "registered_at": current.get("registered_at", now),
                    "updated_at": now,
                    "firmware_version": payload.firmware_version,
                    "hardware": payload.hardware,
                    "ip": payload.ip,
                }
            )
            self._devices[payload.device_id] = current

        return {
            "status": "ok",
            "device_id": payload.device_id,
            "registered": True,
        }

    def heartbeat(self, payload: DeviceHeartbeatRequest) -> dict[str, object]:
        """更新设备最近一次心跳状态。

        如果设备还没有显式注册，本函数会创建一条 registered=false 的临时记录，
        这样设备调试时先发心跳也不会导致服务端报错。后续正式鉴权阶段可以收紧。
        """

        now = self._now_iso()
        with self._lock:
            current = self._devices.get(
                payload.device_id,
                {
                    "device_id": payload.device_id,
                    "registered": False,
                    "registered_at": None,
                },
            )
            current.update(
                {
                    "updated_at": now,
                    "last_heartbeat": now,
                    "firmware_version": payload.firmware_version,
                    "wifi_state": payload.wifi_state,
                    "ip": payload.ip,
                    "storage_state": payload.storage_state,
                    "audio_state": payload.audio_state,
                    "power_state": payload.power_state,
                }
            )
            self._devices[payload.device_id] = current

        return {
            "status": "ok",
            "device_id": payload.device_id,
            "heartbeat": True,
        }

    def get_status(self, device_id: str) -> dict[str, object]:
        """查询设备当前内存状态。

        查询结果来自同一份内存字典，不访问数据库。为了避免调用方误改内部状态，
        返回前会做浅层深拷贝。
        """

        cleaned = device_id.strip()
        if not cleaned:
            return {"status": "error", "message": "device_id must not be empty"}

        with self._lock:
            current = deepcopy(self._devices.get(cleaned))

        if current is None:
            return {
                "status": "not_found",
                "device_id": cleaned,
                "registered": False,
                "last_heartbeat": None,
            }

        return {
            "status": "ok",
            "device_id": cleaned,
            "registered": bool(current.get("registered", False)),
            "last_heartbeat": current.get("last_heartbeat"),
            "firmware_version": current.get("firmware_version"),
            "hardware": current.get("hardware"),
            "ip": current.get("ip"),
            "wifi_state": current.get("wifi_state"),
            "storage_state": current.get("storage_state"),
            "audio_state": current.get("audio_state"),
            "power_state": current.get("power_state"),
        }


registry = DeviceRegistry()

