import json
import subprocess
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

from fastapi import Body, FastAPI, Header, HTTPException, Query, Response
from fastapi.responses import FileResponse
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


class SessionStart(BaseModel):
    device_id: str
    sample_rate_hz: int
    sample_format: str = "pcm_s16le"
    started_at_epoch: Optional[int] = None


class SessionFinish(BaseModel):
    device_id: str
    session_id: str
    completed_at_epoch: Optional[int] = None


def require_device_secret(authorization: str | None) -> None:
    """Reject device calls unless they include the configured bearer token."""
    expected = f"Bearer {settings.yappl_device_secret}"
    if authorization != expected:
        raise HTTPException(status_code=401, detail="invalid device secret")


def now_iso() -> str:
    """Return an easy-to-read UTC timestamp for logs and API responses."""
    return datetime.now(timezone.utc).isoformat()


def storage_root() -> Path:
    """Return the persistent storage folder mounted by Docker Compose."""
    root = Path(settings.yappl_storage_dir)
    root.mkdir(parents=True, exist_ok=True)
    return root


def session_dir(session_id: str) -> Path:
    return storage_root() / "sessions" / session_id


def metadata_path(session_id: str) -> Path:
    return session_dir(session_id) / "metadata.json"


def audio_path(session_id: str) -> Path:
    # Raw little-endian signed 16-bit mono PCM. This is easy for firmware to
    # upload and can later be converted to WAV/MP3 for playback/transcription.
    return session_dir(session_id) / "audio.pcm_s16le"


def mp3_path(session_id: str) -> Path:
    return session_dir(session_id) / "audio.mp3"


def read_metadata(session_id: str) -> dict:
    path = metadata_path(session_id)
    if not path.exists():
      raise HTTPException(status_code=404, detail="session not found")
    return json.loads(path.read_text())


def write_metadata(session_id: str, metadata: dict) -> None:
    folder = session_dir(session_id)
    folder.mkdir(parents=True, exist_ok=True)
    metadata_path(session_id).write_text(json.dumps(metadata, indent=2, sort_keys=True))


def convert_pcm_to_mp3(session_id: str, metadata: dict) -> dict:
    """Encode the raw PCM session file into an MP3 using FFmpeg."""
    source = audio_path(session_id)
    target = mp3_path(session_id)
    if not source.exists() or source.stat().st_size == 0:
        metadata["mp3_status"] = "skipped_empty_pcm"
        return metadata

    command = [
        "ffmpeg",
        "-y",
        "-f",
        "s16le",
        "-ar",
        str(metadata["sample_rate_hz"]),
        "-ac",
        "1",
        "-i",
        str(source),
        "-codec:a",
        "libmp3lame",
        "-b:a",
        settings.yappl_mp3_bitrate,
        str(target),
    ]

    try:
        result = subprocess.run(command, capture_output=True, text=True, check=False)
    except FileNotFoundError:
        metadata["mp3_status"] = "ffmpeg_missing"
        return metadata

    if result.returncode != 0:
        metadata["mp3_status"] = "failed"
        metadata["mp3_error"] = result.stderr[-1000:]
        return metadata

    metadata["mp3_status"] = "ready"
    metadata["mp3_file"] = str(target)
    metadata["mp3_bytes"] = target.stat().st_size
    metadata["mp3_bitrate"] = settings.yappl_mp3_bitrate
    metadata["mp3_created_at"] = now_iso()
    return metadata


@app.get("/health")
def health() -> dict:
    """Simple route for confirming the backend process is alive."""
    return {
        "status": "ok",
        "environment": settings.yappl_env,
        "server_time": now_iso(),
    }


@app.post("/device/session/start")
def session_start(payload: SessionStart, authorization: str | None = Header(default=None)) -> dict:
    """Create a durable audio session folder and metadata file."""
    require_device_secret(authorization)

    session_id = f"session_{uuid.uuid4().hex}"
    metadata = {
        "session_id": session_id,
        "device_id": payload.device_id,
        "sample_rate_hz": payload.sample_rate_hz,
        "sample_format": payload.sample_format,
        "started_at": now_iso(),
        "started_at_epoch": payload.started_at_epoch,
        "completed_at": None,
        "completed_at_epoch": None,
        "audio_bytes": 0,
        "audio_file": str(audio_path(session_id)),
        "mp3_status": "not_created",
        "mp3_file": None,
        "mp3_bytes": 0,
    }
    write_metadata(session_id, metadata)
    audio_path(session_id).write_bytes(b"")

    print("session started:", payload.device_id, session_id, flush=True)
    return {
        "status": "ok",
        "session_id": session_id,
        "server_time": now_iso(),
    }


@app.post("/device/session/audio")
async def session_audio(
    session_id: str = Query(...),
    authorization: str | None = Header(default=None),
    body: bytes = Body(...),
) -> dict:
    """Append one raw PCM audio chunk to a session's audio file."""
    require_device_secret(authorization)

    metadata = read_metadata(session_id)
    with audio_path(session_id).open("ab") as audio_file:
        audio_file.write(body)

    metadata["audio_bytes"] = int(metadata.get("audio_bytes", 0)) + len(body)
    metadata["last_audio_chunk_at"] = now_iso()
    write_metadata(session_id, metadata)

    return {
        "status": "ok",
        "session_id": session_id,
        "received_bytes": len(body),
        "audio_bytes": metadata["audio_bytes"],
    }


@app.post("/device/session/finish")
def session_finish(payload: SessionFinish, authorization: str | None = Header(default=None)) -> dict:
    """Mark a durable audio session complete."""
    require_device_secret(authorization)

    metadata = read_metadata(payload.session_id)
    if metadata["device_id"] != payload.device_id:
        raise HTTPException(status_code=400, detail="device/session mismatch")

    metadata["completed_at"] = now_iso()
    metadata["completed_at_epoch"] = payload.completed_at_epoch
    metadata = convert_pcm_to_mp3(payload.session_id, metadata)
    write_metadata(payload.session_id, metadata)

    state = device_state.setdefault(payload.device_id, {})
    state["last_yap_completed_at"] = metadata["completed_at"]
    state["last_yap_completed_at_epoch"] = payload.completed_at_epoch
    state["last_session_id"] = payload.session_id
    state["has_yapped_today"] = True

    print(
        "session finished:",
        payload.device_id,
        payload.session_id,
        f"bytes={metadata['audio_bytes']}",
        flush=True,
    )
    return {
        "status": "ok",
        "session_id": payload.session_id,
        "audio_bytes": metadata["audio_bytes"],
        "mp3_status": metadata.get("mp3_status"),
        "mp3_bytes": metadata.get("mp3_bytes", 0),
        "has_yapped_today": True,
    }


@app.get("/device/session/{session_id}")
def session_metadata(session_id: str, authorization: str | None = Header(default=None)) -> dict:
    """Return saved metadata for one session."""
    require_device_secret(authorization)
    return read_metadata(session_id)


@app.get("/device/session/{session_id}/audio")
def session_audio_download(session_id: str, authorization: str | None = Header(default=None)) -> Response:
    """Download the raw PCM audio saved for a session."""
    require_device_secret(authorization)
    read_metadata(session_id)
    return FileResponse(
        audio_path(session_id),
        media_type="application/octet-stream",
        filename=f"{session_id}.pcm_s16le",
    )


@app.get("/device/session/{session_id}/audio.mp3")
def session_mp3_download(session_id: str, authorization: str | None = Header(default=None)) -> Response:
    """Download the MP3 created when the session finished."""
    require_device_secret(authorization)
    metadata = read_metadata(session_id)
    if metadata.get("mp3_status") != "ready" or not mp3_path(session_id).exists():
        raise HTTPException(status_code=404, detail="mp3 not ready")
    return FileResponse(
        mp3_path(session_id),
        media_type="audio/mpeg",
        filename=f"{session_id}.mp3",
    )


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
        "last_session_id": state.get("last_session_id"),
        "server_time": now_iso(),
    }
