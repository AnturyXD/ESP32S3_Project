"""服务端配置集中管理。

这里统一读取环境变量，避免在路由或业务模块里散落读取 os.environ。
运行时摘要接口只允许返回非敏感配置，以及密钥“是否已配置”的布尔值，
绝不能返回 API Key、Token、Password 原文。
"""

from functools import lru_cache

from pydantic import Field
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    """服务器运行配置。

    S0.4 默认绑定 127.0.0.1:18080，这是为了避让服务器上的 Carshow
    项目和公网入口。后续如果要开放公网，必须先单独审计 UFW、iptables、
    云安全组和反向代理配置。
    """

    app_name: str = Field(default="esp-ai-terminal-server", alias="APP_NAME")
    app_version: str = Field(default="0.1.0", alias="APP_VERSION")
    app_stage: str = Field(default="S0.4", alias="APP_STAGE")
    app_host: str = Field(default="127.0.0.1", alias="APP_HOST")
    app_port: int = Field(default=18080, alias="APP_PORT")
    log_level: str = Field(default="INFO", alias="LOG_LEVEL")
    ai_provider: str = Field(default="placeholder", alias="AI_PROVIDER")
    device_shared_secret: str = Field(default="CHANGE_ME", alias="DEVICE_SHARED_SECRET")
    asr_api_key: str = Field(default="", alias="ASR_API_KEY")
    llm_api_key: str = Field(default="", alias="LLM_API_KEY")
    tts_api_key: str = Field(default="", alias="TTS_API_KEY")

    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        extra="ignore",
        populate_by_name=True,
    )

    @property
    def device_auth_configured(self) -> bool:
        """判断设备共享密钥是否真正配置。

        `.env.example` 里的 CHANGE_ME 只是占位符，不能被当作真实密钥。
        如果把占位符也视为已配置，开发阶段会出现“看似配置了鉴权，
        实际所有设备都知道默认 token”的安全错觉。
        """

        secret = self.device_shared_secret.strip()
        return bool(secret) and secret != "CHANGE_ME"

    def runtime_audit_summary(self) -> dict[str, object]:
        """生成可公开给本地审计接口的运行摘要。

        注意：这里刻意只返回密钥是否存在，不返回任何密钥内容。
        这个接口用于部署前后人工确认服务是否按预期绑定本机端口。
        """

        return {
            "APP_NAME": self.app_name,
            "APP_VERSION": self.app_version,
            "APP_STAGE": self.app_stage,
            "APP_HOST": self.app_host,
            "APP_PORT": self.app_port,
            "LOG_LEVEL": self.log_level,
            "AI_PROVIDER": self.ai_provider,
            "ASR_API_KEY_CONFIGURED": bool(self.asr_api_key),
            "LLM_API_KEY_CONFIGURED": bool(self.llm_api_key),
            "TTS_API_KEY_CONFIGURED": bool(self.tts_api_key),
            "DEVICE_SHARED_SECRET_CONFIGURED": self.device_auth_configured,
            "device_auth_configured": self.device_auth_configured,
        }


@lru_cache(maxsize=1)
def get_settings() -> Settings:
    """返回全局配置单例。

    FastAPI 进程内多处读取配置时复用同一个对象，减少重复解析环境变量。
    """

    return Settings()
