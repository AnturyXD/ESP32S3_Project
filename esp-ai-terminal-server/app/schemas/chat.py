"""Chat 接口请求和响应结构。

V0.7 只做单轮文本对话代理，不保存长期上下文、不接数据库、不返回音频。
设备端只能把 ASR final text 发给自建服务器，不能直接访问火山方舟。
"""

from pydantic import BaseModel, Field, field_validator


class ChatRequest(BaseModel):
    """设备发起的单轮 Chat 请求。"""

    device_id: str = Field(..., description="设备唯一 ID，不能为空")
    text: str = Field(..., description="ASR final text 或手动测试文本")
    source: str = Field(default="asr", description="文本来源：asr/manual")
    language: str = Field(default="auto", description="语言策略，默认自动跟随输入")

    @field_validator("device_id")
    @classmethod
    def validate_device_id(cls, value: str) -> str:
        """确保设备 ID 非空，避免内存状态互相覆盖。"""

        cleaned = value.strip()
        if not cleaned:
            raise ValueError("device_id must not be empty")
        return cleaned

    @field_validator("text")
    @classmethod
    def validate_text(cls, value: str) -> str:
        """确保请求文本非空。长度限制在 API 层结合配置判断。"""

        cleaned = value.strip()
        if not cleaned:
            raise ValueError("text must not be empty")
        return cleaned


class ChatResponse(BaseModel):
    """Chat 响应。

    reply_text 可能包含中文，ESP32 屏幕不要求显示原文；设备端会在串口保留
    UTF-8 文本，并在 UI 上降级显示 Response received。
    """

    status: str
    device_id: str
    reply_text: str = ""
    reply_received: bool = False
    message: str | None = None
