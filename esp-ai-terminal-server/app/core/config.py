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

    V0.6 默认绑定 127.0.0.1:18080，这是为了避让服务器上的 Carshow
    项目和公网入口。后续如果要开放公网，必须先单独审计 UFW、iptables、
    云安全组和反向代理配置。
    """

    app_name: str = Field(default="esp-ai-terminal-server", alias="APP_NAME")
    app_version: str = Field(default="0.1.0", alias="APP_VERSION")
    app_stage: str = Field(default="V0.6", alias="APP_STAGE")
    app_host: str = Field(default="127.0.0.1", alias="APP_HOST")
    app_port: int = Field(default=18080, alias="APP_PORT")
    log_level: str = Field(default="INFO", alias="LOG_LEVEL")
    ai_provider: str = Field(default="placeholder", alias="AI_PROVIDER")
    device_shared_secret: str = Field(default="CHANGE_ME", alias="DEVICE_SHARED_SECRET")
    asr_provider: str = Field(default="volcengine", alias="ASR_PROVIDER")
    asr_ws_url: str = Field(
        default="wss://openspeech.bytedance.com/api/v3/sauc/bigmodel_async",
        alias="ASR_WS_URL",
    )
    asr_api_key: str = Field(default="", alias="ASR_API_KEY")
    asr_app_key: str = Field(default="", alias="ASR_APP_KEY")
    asr_access_key: str = Field(default="", alias="ASR_ACCESS_KEY")
    asr_resource_id: str = Field(
        default="volc.bigasr.sauc.duration",
        alias="ASR_RESOURCE_ID",
    )
    asr_audio_format: str = Field(default="pcm", alias="ASR_AUDIO_FORMAT")
    asr_sample_rate: int = Field(default=16000, alias="ASR_SAMPLE_RATE")
    asr_bits: int = Field(default=16, alias="ASR_BITS")
    asr_channels: int = Field(default=1, alias="ASR_CHANNELS")
    asr_packet_ms: int = Field(default=200, alias="ASR_PACKET_MS")
    llm_provider: str = Field(default="volcengine_ark", alias="LLM_PROVIDER")
    ark_api_key: str = Field(default="", alias="ARK_API_KEY")
    ark_base_url: str = Field(
        default="https://ark.cn-beijing.volces.com/api/v3",
        alias="ARK_BASE_URL",
    )
    llm_model: str = Field(default="", alias="LLM_MODEL")
    llm_api_key: str = Field(default="", alias="LLM_API_KEY")
    llm_timeout_seconds: int = Field(default=30, alias="LLM_TIMEOUT_SECONDS")
    llm_max_input_chars: int = Field(default=1000, alias="LLM_MAX_INPUT_CHARS")
    llm_max_output_tokens: int = Field(default=512, alias="LLM_MAX_OUTPUT_TOKENS")
    tts_provider: str = Field(default="volcengine", alias="TTS_PROVIDER")
    tts_model: str = Field(default="seed-tts-2.0", alias="TTS_MODEL")
    tts_api_version: str = Field(default="v3", alias="TTS_API_VERSION")
    tts_ws_url: str = Field(default="", alias="TTS_WS_URL")
    tts_app_id: str = Field(default="", alias="TTS_APP_ID")
    tts_access_token: str = Field(default="", alias="TTS_ACCESS_TOKEN")
    tts_api_key: str = Field(default="", alias="TTS_API_KEY")
    tts_cluster: str = Field(default="", alias="TTS_CLUSTER")
    tts_resource_id: str = Field(default="seed-tts-2.0", alias="TTS_RESOURCE_ID")
    tts_voice_type: str = Field(default="", alias="TTS_VOICE_TYPE")
    tts_audio_format: str = Field(default="pcm", alias="TTS_AUDIO_FORMAT")
    tts_sample_rate: int = Field(default=16000, alias="TTS_SAMPLE_RATE")
    tts_bits: int = Field(default=16, alias="TTS_BITS")
    tts_channels: int = Field(default=1, alias="TTS_CHANNELS")
    tts_max_text_chars: int = Field(default=300, alias="TTS_MAX_TEXT_CHARS")
    tts_timeout_seconds: int = Field(default=30, alias="TTS_TIMEOUT_SECONDS")

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

    @property
    def asr_api_key_configured(self) -> bool:
        """判断火山 ASR Key 是否已配置。

        API Key 只允许在服务器内部连接火山 ASR 时使用。对外接口和日志只能返回
        true/false，不能返回原文或截断片段，避免截图、日志采集或误传仓库造成泄露。
        """

        return bool(self.asr_api_key.strip())

    @property
    def asr_app_access_key_configured(self) -> bool:
        """判断火山流式 ASR 官方 AppKey/AccessKey 是否已配置。

        火山不同 ASR 接口的鉴权字段可能不同：部分接口使用 `X-Api-Key`，
        大模型流式 ASR 文档常见写法是 `X-Api-App-Key` + `X-Api-Access-Key`。
        为了让 V0.6 smoke test 更容易排查，两个字段都支持，但都不对外返回原文。
        """

        return bool(self.asr_app_key.strip()) and bool(self.asr_access_key.strip())

    @property
    def asr_auth_configured(self) -> bool:
        """判断是否至少存在一种 ASR 鉴权配置。"""

        return self.asr_api_key_configured or self.asr_app_access_key_configured

    @property
    def asr_auth_mode(self) -> str:
        """返回当前 ASR 鉴权模式摘要，不包含任何密钥内容。"""

        if self.asr_app_access_key_configured:
            return "app_access_key"
        if self.asr_api_key_configured:
            return "api_key"
        return "missing"

    @property
    def asr_configured(self) -> bool:
        """判断 ASR 自测所需配置是否完整。

        V0.6 只做服务器侧 smoke test，不接 ESP32 音频上传。这里把 WebSocket
        地址、Resource ID、音频参数和 Key 都纳入检查，缺任何一项都返回
        Config Missing，避免脚本或接口在半配置状态下崩溃。
        """

        return all(
            [
                self.asr_provider.strip(),
                self.asr_ws_url.strip(),
                self.asr_auth_configured,
                self.asr_resource_id.strip(),
                self.asr_audio_format.strip(),
                self.asr_sample_rate > 0,
                self.asr_bits > 0,
                self.asr_channels > 0,
                self.asr_packet_ms > 0,
            ]
        )

    def asr_public_config_summary(self) -> dict[str, object]:
        """返回 ASR 非敏感配置摘要。

        该摘要用于 `/api/esp-ai-terminal/asr/config`，方便在服务器本机 curl
        检查 ASR 参数是否读到。注意它永远不返回 ASR_API_KEY 原文。
        """

        return {
            "status": "ok" if self.asr_configured else "Config Missing",
            "provider": self.asr_provider,
            "configured": self.asr_configured,
            "ws_url": self.asr_ws_url,
            "resource_id": self.asr_resource_id,
            "audio_format": self.asr_audio_format,
            "sample_rate": self.asr_sample_rate,
            "bits": self.asr_bits,
            "channels": self.asr_channels,
            "packet_ms": self.asr_packet_ms,
            "api_key_configured": self.asr_api_key_configured,
            "app_key_configured": bool(self.asr_app_key.strip()),
            "access_key_configured": bool(self.asr_access_key.strip()),
            "auth_mode": self.asr_auth_mode,
        }

    @property
    def ark_api_key_configured(self) -> bool:
        """判断火山方舟 API Key 是否已配置。

        V0.7 的 LLM 代理只允许服务器读取 ARK_API_KEY。对外接口只能返回
        true/false，不能返回原文或截断片段，避免公网临时调试时泄露密钥。
        """

        return bool(self.ark_api_key.strip())

    @property
    def llm_configured(self) -> bool:
        """判断火山方舟 LLM 调用所需配置是否完整。"""

        return all(
            [
                self.llm_provider.strip(),
                self.ark_api_key_configured,
                self.ark_base_url.strip(),
                self.llm_model.strip(),
                self.llm_timeout_seconds > 0,
                self.llm_max_input_chars > 0,
                self.llm_max_output_tokens > 0,
            ]
        )

    def llm_public_config_summary(self) -> dict[str, object]:
        """返回 LLM 非敏感配置摘要。

        该摘要用于 `/api/esp-ai-terminal/llm/config`。模型接入点 ID 在部分团队
        里也可能被视为内部信息，因此这里只返回 model_configured 布尔值。
        """

        return {
            "status": "ok" if self.llm_configured else "Config Missing",
            "provider": self.llm_provider,
            "configured": self.llm_configured,
            "base_url": self.ark_base_url,
            "model_configured": bool(self.llm_model.strip()),
            "api_key_configured": self.ark_api_key_configured,
            "timeout_seconds": self.llm_timeout_seconds,
            "max_input_chars": self.llm_max_input_chars,
            "max_output_tokens": self.llm_max_output_tokens,
        }

    @property
    def tts_api_key_configured(self) -> bool:
        """判断新版控制台 TTS API Key 是否已配置。

        新版豆包语音合成 V3 推荐使用 X-Api-Key + X-Api-Resource-Id。
        这里只返回布尔值，绝不把 Key 原文暴露给审计接口或日志。
        """

        return bool(self.tts_api_key.strip())

    @property
    def tts_app_token_configured(self) -> bool:
        """判断旧版控制台 AppID / AccessToken 鉴权是否已配置。"""

        return bool(self.tts_app_id.strip()) and bool(self.tts_access_token.strip())

    @property
    def tts_credential_configured(self) -> bool:
        """判断是否存在一种可用的 TTS 鉴权方式。"""

        return self.tts_api_key_configured or self.tts_app_token_configured

    @property
    def tts_auth_mode(self) -> str:
        """返回 TTS 鉴权模式摘要，不包含密钥内容。"""

        if self.tts_api_key_configured:
            return "api_key"
        if self.tts_app_token_configured:
            return "app_access_token"
        return "missing"

    @property
    def tts_effective_url(self) -> str:
        """返回 V0.8 使用的火山 TTS HTTP Chunked 接口地址。

        历史字段名保留为 TTS_WS_URL，但 V0.8 明确采用 HTTP Chunked
        单向流式接口，未配置时使用官方 V3 HTTP endpoint。
        """

        return self.tts_ws_url.strip() or "https://openspeech.bytedance.com/api/v3/tts/unidirectional"

    @property
    def tts_configured(self) -> bool:
        """判断 TTS 合成所需配置是否完整。

        V0.8 仅支持返回 ESP32 可直接播放的 PCM/WAV PCM。音色 speaker
        是官方接口必填项，因此也纳入配置完整性检查。
        """

        return all(
            [
                self.tts_provider.strip(),
                self.tts_resource_id.strip(),
                self.tts_credential_configured,
                self.tts_voice_type.strip(),
                self.tts_audio_format.strip(),
                self.tts_sample_rate > 0,
                self.tts_bits == 16,
                self.tts_channels == 1,
                self.tts_max_text_chars > 0,
                self.tts_timeout_seconds > 0,
            ]
        )

    def tts_public_config_summary(self) -> dict[str, object]:
        """返回 TTS 非敏感配置摘要。

        用于 `/api/esp-ai-terminal/tts/config`。该摘要只说明配置是否齐全，
        不返回 API Key、Access Token、App ID、音色原文等可能敏感的信息。
        """

        return {
            "status": "ok" if self.tts_configured else "Config Missing",
            "provider": self.tts_provider,
            "configured": self.tts_configured,
            "model": self.tts_model,
            "api_version": self.tts_api_version,
            "voice_configured": bool(self.tts_voice_type.strip()),
            "audio_format": self.tts_audio_format,
            "sample_rate": self.tts_sample_rate,
            "bits": self.tts_bits,
            "channels": self.tts_channels,
            "max_text_chars": self.tts_max_text_chars,
            "timeout_seconds": self.tts_timeout_seconds,
            "credential_configured": self.tts_credential_configured,
            "auth_mode": self.tts_auth_mode,
        }

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
            "ASR_PROVIDER": self.asr_provider,
            "ASR_API_KEY_CONFIGURED": self.asr_api_key_configured,
            "asr_api_key_configured": self.asr_api_key_configured,
            "asr_app_key_configured": bool(self.asr_app_key.strip()),
            "asr_access_key_configured": bool(self.asr_access_key.strip()),
            "asr_auth_mode": self.asr_auth_mode,
            "asr_configured": self.asr_configured,
            "LLM_API_KEY_CONFIGURED": bool(self.llm_api_key or self.ark_api_key),
            "ark_api_key_configured": self.ark_api_key_configured,
            "llm_configured": self.llm_configured,
            "TTS_API_KEY_CONFIGURED": bool(self.tts_api_key),
            "tts_configured": self.tts_configured,
            "tts_key_configured": self.tts_credential_configured,
            "DEVICE_SHARED_SECRET_CONFIGURED": self.device_auth_configured,
            "device_auth_configured": self.device_auth_configured,
        }


@lru_cache(maxsize=1)
def get_settings() -> Settings:
    """返回全局配置单例。

    FastAPI 进程内多处读取配置时复用同一个对象，减少重复解析环境变量。
    """

    return Settings()

