"""TTS 接口请求结构。

V0.8 只做“文本 -> 服务器 TTS -> PCM/WAV PCM -> ESP32 播放”的最小闭环。
设备端不保存火山 TTS 密钥，也不直接访问火山 TTS。
"""

from pydantic import BaseModel, Field, field_validator


class TtsSynthesizeRequest(BaseModel):
    """设备请求服务器合成语音的请求体。"""

    device_id: str = Field(..., description="设备唯一 ID，不能为空")
    text: str = Field(..., description="需要合成的回复文本")
    voice_type: str = Field(default="", description="可选音色覆盖；为空时使用服务器 .env")
    audio_format: str = Field(default="pcm", description="V0.8 仅支持 pcm 或 wav")
    sample_rate: int = Field(default=16000, description="V0.8 固定 16000Hz")

    @field_validator("device_id")
    @classmethod
    def validate_device_id(cls, value: str) -> str:
        cleaned = value.strip()
        if not cleaned:
            raise ValueError("device_id must not be empty")
        return cleaned

    @field_validator("text")
    @classmethod
    def validate_text(cls, value: str) -> str:
        cleaned = value.strip()
        if not cleaned:
            raise ValueError("text must not be empty")
        return cleaned

    @field_validator("audio_format")
    @classmethod
    def validate_audio_format(cls, value: str) -> str:
        cleaned = value.strip().lower()
        if cleaned not in {"pcm", "wav"}:
            raise ValueError("audio_format must be pcm or wav")
        return cleaned
