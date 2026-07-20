from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    """Environment-backed settings for the local Yappl backend."""

    yappl_env: str = "local"
    yappl_device_secret: str = "local_dev_secret"
    yappl_storage_dir: str = "/data"
    yappl_mp3_bitrate: str = "64k"
    yappl_transcription_enabled: bool = True
    yappl_whisper_cli: str = "/usr/local/bin/whisper-cli"
    yappl_whisper_model: str = "/opt/whisper/models/ggml-base.en.bin"
    yappl_whisper_language: str = "en"
    yappl_transcription_timeout_seconds: int = 1800
    yappl_summary_enabled: bool = True
    yappl_summary_timeout_seconds: int = 120
    yappl_settings_secret: str = "change-me-before-storing-api-keys"

    # Environment keys are a convenient fallback. Keys saved through the
    # settings API are encrypted on disk and take precedence over these.
    openai_api_key: str = ""
    anthropic_api_key: str = ""
    google_api_key: str = ""
    deepseek_api_key: str = ""
    groq_api_key: str = ""
    openrouter_api_key: str = ""
    together_api_key: str = ""
    fireworks_api_key: str = ""
    mistral_api_key: str = ""
    xai_api_key: str = ""

    model_config = SettingsConfigDict(env_file=".env", extra="ignore")


settings = Settings()
