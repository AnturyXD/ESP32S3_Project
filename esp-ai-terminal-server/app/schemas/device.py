"""设备相关请求数据结构。

S0.4 只做最小注册和心跳验证，请求体全部通过 Pydantic 校验。
这里不会包含任何密钥字段，避免设备状态接口误记录 Token、API Key 或密码。
"""

from pydantic import BaseModel, Field, field_validator


class DeviceRegisterRequest(BaseModel):
    """设备注册请求。

    当前只登记设备基础信息，后续 S0.4+ 才会增加鉴权、设备密钥和持久化存储。
    """

    device_id: str = Field(..., description="设备唯一 ID，不能为空")
    firmware_version: str = Field(..., description="固件版本")
    hardware: str = Field(default="ESP32-S3-Touch-LCD-3.49", description="硬件型号")
    ip: str | None = Field(default=None, description="设备当前局域网 IP，仅用于调试显示")

    @field_validator("device_id")
    @classmethod
    def validate_device_id(cls, value: str) -> str:
        """确保设备 ID 非空。

        设备 ID 是内存状态表的键，如果允许空字符串，会导致多个设备互相覆盖。
        """

        cleaned = value.strip()
        if not cleaned:
            raise ValueError("device_id must not be empty")
        return cleaned


class DeviceHeartbeatRequest(BaseModel):
    """设备心跳请求。

    心跳只上报运行状态，不携带敏感密钥。服务器暂时只保存在内存中，
    用于验证 ESP32 设备到服务器代理的最小链路。
    """

    device_id: str = Field(..., description="设备唯一 ID，不能为空")
    firmware_version: str | None = None
    wifi_state: str | None = None
    ip: str | None = None
    storage_state: str | None = None
    audio_state: str | None = None
    power_state: str | None = None

    @field_validator("device_id")
    @classmethod
    def validate_device_id(cls, value: str) -> str:
        """确保心跳中的设备 ID 非空。"""

        cleaned = value.strip()
        if not cleaned:
            raise ValueError("device_id must not be empty")
        return cleaned

