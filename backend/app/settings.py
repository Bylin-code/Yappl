from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    """Environment-backed settings for the local Yappl backend."""

    yappl_env: str = "local"
    yappl_device_secret: str = "local_dev_secret"

    model_config = SettingsConfigDict(env_file=".env", extra="ignore")


settings = Settings()

