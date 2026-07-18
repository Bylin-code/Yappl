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

    model_config = SettingsConfigDict(env_file=".env", extra="ignore")


settings = Settings()
