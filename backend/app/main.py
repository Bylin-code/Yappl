from datetime import datetime, timezone
from typing import Optional

from fastapi import FastAPI, Header, HTTPException
from pydantic import BaseModel

from .settings import settings


app = FastAPI(title="Yappl Backend")

# This is intentionally in-memory for the first connection milestone. It proves
# the ESP32 can reach the backend before we add Postgres or cloud storage.
device_state: dict[str, dict] = {}


class DevicePing(BaseModel):
    device_id: str
    wifi_connected: bool
    time_synced: bool
    mode: Optional[str] = None


class YapCompleted(BaseModel):
    device_id: str
    completed_at_epoch: Optional[int] = None


def require_device_secret(authorization: str | None) -> None:
    """Reject device calls unless they include the configured bearer token."""
    expected = f"Bearer {settings.yappl_device_secret}"
    if authorization != expected:
        raise HTTPException(status_code=401, detail="invalid device secret")


def now_iso() -> str:
    """Return an easy-to-read UTC timestamp for logs and API responses."""
    return datetime.now(timezone.utc).isoformat()


@app.get("/health")
def health() -> dict:
    """Simple route for confirming the backend process is alive."""
    return {
        "status": "ok",
        "environment": settings.yappl_env,
        "server_time": now_iso(),
    }


@app.post("/device/ping")
def device_ping(payload: DevicePing, authorization: str | None = Header(default=None)) -> dict:
    """Called by Yappl so the backend knows the device is online."""
    require_device_secret(authorization)

    state = device_state.setdefault(payload.device_id, {})
    state["last_seen_at"] = now_iso()
    state["wifi_connected"] = payload.wifi_connected
    state["time_synced"] = payload.time_synced
    state["mode"] = payload.mode

    print(
        "device ping received:",
        payload.device_id,
        f"wifi={payload.wifi_connected}",
        f"time={payload.time_synced}",
        f"mode={payload.mode}",
        flush=True,
    )

    return {
        "status": "ok",
        "server_time": now_iso(),
    }


@app.post("/device/yap-completed")
def yap_completed(payload: YapCompleted, authorization: str | None = Header(default=None)) -> dict:
    """Called when the device finishes a yap session."""
    require_device_secret(authorization)

    state = device_state.setdefault(payload.device_id, {})
    state["last_yap_completed_at"] = now_iso()
    state["last_yap_completed_at_epoch"] = payload.completed_at_epoch
    state["has_yapped_today"] = True

    print(
        "yap completed:",
        payload.device_id,
        f"epoch={payload.completed_at_epoch}",
        flush=True,
    )

    return {
        "status": "ok",
        "has_yapped_today": True,
        "server_time": now_iso(),
    }


@app.get("/device/status")
def device_status(device_id: str, authorization: str | None = Header(default=None)) -> dict:
    """Returns the backend's current memory of this device."""
    require_device_secret(authorization)

    state = device_state.get(device_id, {})
    return {
        "device_id": device_id,
        "has_yapped_today": bool(state.get("has_yapped_today", False)),
        "last_seen_at": state.get("last_seen_at"),
        "last_yap_completed_at": state.get("last_yap_completed_at"),
        "server_time": now_iso(),
    }

