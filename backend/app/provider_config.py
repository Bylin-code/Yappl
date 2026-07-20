import base64
import hashlib
import json
from pathlib import Path

from cryptography.fernet import Fernet, InvalidToken

from .settings import settings


PROVIDERS = {
    "openai": {"label": "OpenAI", "kind": "openai", "base_url": "https://api.openai.com/v1", "default_model": "gpt-5-nano", "env_key": "openai_api_key"},
    "anthropic": {"label": "Anthropic", "kind": "anthropic", "default_model": "claude-sonnet-5", "env_key": "anthropic_api_key"},
    "gemini": {"label": "Google Gemini", "kind": "gemini", "default_model": "gemini-2.5-flash", "env_key": "google_api_key"},
    "deepseek": {"label": "DeepSeek", "kind": "openai", "base_url": "https://api.deepseek.com", "default_model": "deepseek-chat", "env_key": "deepseek_api_key"},
    "groq": {"label": "Groq", "kind": "openai", "base_url": "https://api.groq.com/openai/v1", "default_model": "llama-3.3-70b-versatile", "env_key": "groq_api_key"},
    "openrouter": {"label": "OpenRouter", "kind": "openai", "base_url": "https://openrouter.ai/api/v1", "default_model": "openai/gpt-5-nano", "env_key": "openrouter_api_key"},
    "together": {"label": "Together AI", "kind": "openai", "base_url": "https://api.together.xyz/v1", "default_model": "meta-llama/Llama-3.3-70B-Instruct-Turbo", "env_key": "together_api_key"},
    "fireworks": {"label": "Fireworks AI", "kind": "openai", "base_url": "https://api.fireworks.ai/inference/v1", "default_model": "accounts/fireworks/models/llama-v3p3-70b-instruct", "env_key": "fireworks_api_key"},
    "mistral": {"label": "Mistral AI", "kind": "openai", "base_url": "https://api.mistral.ai/v1", "default_model": "mistral-small-latest", "env_key": "mistral_api_key"},
    "xai": {"label": "xAI", "kind": "openai", "base_url": "https://api.x.ai/v1", "default_model": "grok-3-mini", "env_key": "xai_api_key"},
    "ollama": {"label": "Ollama (local)", "kind": "ollama", "base_url": "http://host.docker.internal:11434", "default_model": "qwen3:8b", "env_key": None},
}


def _config_path() -> Path:
    path = Path(settings.yappl_storage_dir) / "summary-provider.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def _cipher() -> Fernet:
    digest = hashlib.sha256(settings.yappl_settings_secret.encode()).digest()
    return Fernet(base64.urlsafe_b64encode(digest))


def load_provider_config() -> dict:
    path = _config_path()
    if not path.exists():
        return {"provider": "anthropic", "model": PROVIDERS["anthropic"]["default_model"]}
    return json.loads(path.read_text())


def save_provider_config(provider: str, model: str, api_key: str | None, base_url: str | None) -> dict:
    if provider not in PROVIDERS:
        raise ValueError("unsupported provider")
    previous = load_provider_config()
    encrypted_key = previous.get("encrypted_api_key") if previous.get("provider") == provider else None
    if api_key:
        encrypted_key = _cipher().encrypt(api_key.encode()).decode()
    config = {"provider": provider, "model": model or PROVIDERS[provider]["default_model"]}
    if encrypted_key:
        config["encrypted_api_key"] = encrypted_key
    if base_url:
        config["base_url"] = base_url.rstrip("/")
    _config_path().write_text(json.dumps(config, indent=2))
    return public_provider_config(config)


def provider_api_key(config: dict) -> str:
    encrypted = config.get("encrypted_api_key")
    if encrypted:
        try:
            return _cipher().decrypt(encrypted.encode()).decode()
        except InvalidToken as error:
            raise ValueError("stored API key cannot be decrypted; YAPPL_SETTINGS_SECRET changed") from error
    env_name = PROVIDERS[config["provider"]].get("env_key")
    return getattr(settings, env_name, "") if env_name else ""


def public_provider_config(config: dict | None = None) -> dict:
    config = config or load_provider_config()
    key = provider_api_key(config)
    return {
        "provider": config["provider"],
        "model": config.get("model") or PROVIDERS[config["provider"]]["default_model"],
        "base_url": config.get("base_url"),
        "api_key_configured": bool(key),
        "api_key_hint": f"...{key[-4:]}" if key else None,
    }
