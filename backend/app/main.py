import json
import re
import sqlite3
import subprocess
import threading
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

from fastapi import Body, FastAPI, Header, HTTPException, Query, Response
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from .settings import settings
from .memory import canonicalize_known_aliases, correct_transcript, delete_entity, learn_from_session, list_entities, list_pending_entities, promote_pending_entity, upsert_entity
from .provider_config import PROVIDERS, public_provider_config, save_provider_config
from .period_summaries import ensure_period_summaries
from .summarization import summarize_text, summarize_transcript, summary_path
from .transcription import transcribe_mp3, transcript_path
from .session_store import (
    append_chunk,
    claim_jobs,
    enqueue_job,
    finish_job,
    initialize as initialize_session_store,
    load_metadata,
    register_session,
    recover_interrupted_jobs,
    save_metadata,
)


app = FastAPI(title="Yappl Backend")
processing_wakeup = threading.Event()
WEB_ROOT = Path(__file__).parent / "web"
app.mount("/static", StaticFiles(directory=WEB_ROOT), name="static")

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
    sample_rate_hz: int = Field(ge=8000, le=48000)
    sample_format: str = "pcm_s16le"
    started_at_epoch: Optional[int] = None


class SessionFinish(BaseModel):
    device_id: str
    session_id: str
    completed_at_epoch: Optional[int] = None


class SummarySettingsUpdate(BaseModel):
    provider: str
    model: str = ""
    api_key: Optional[str] = None
    base_url: Optional[str] = None


class MemoryFactInput(BaseModel):
    id: Optional[str] = None
    predicate: str
    value: str
    confidence: float = 1.0
    source_session_id: Optional[str] = None
    status: str = "active"


class MemoryEntityInput(BaseModel):
    id: Optional[str] = None
    type: str
    canonical_name: str
    description: str = ""
    aliases: list[str] = Field(default_factory=list)
    facts: list[MemoryFactInput] = Field(default_factory=list)


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


def safe_device_id(device_id: str) -> str:
    """Keep device IDs usable as folder names without allowing path traversal."""
    return "".join(char if char.isalnum() or char in ("-", "_") else "_" for char in device_id)


def device_state_path(device_id: str) -> Path:
    return device_root(device_id) / "state.json"


def device_root(device_id: str) -> Path:
    return storage_root() / "devices" / safe_device_id(device_id)


def read_device_state(device_id: str) -> dict:
    """Load durable device state from disk, then mirror it in memory."""
    path = device_state_path(device_id)
    if path.exists():
        state = json.loads(path.read_text())
    else:
        state = device_state.get(device_id, {})
    device_state[device_id] = state
    return state


def write_device_state(device_id: str, state: dict) -> None:
    """Persist device state so rebooted devices/backend containers remember it."""
    path = device_state_path(device_id)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(state, indent=2, sort_keys=True))
    device_state[device_id] = state


def device_sessions_root(device_id: str) -> Path:
    root = device_root(device_id) / "sessions"
    root.mkdir(parents=True, exist_ok=True)
    return root


def legacy_sessions_root() -> Path:
    return storage_root() / "sessions"


def session_snapshot(metadata: dict) -> dict:
    """Return the session fields that belong in a device state snapshot."""
    return {
        "session_id": metadata.get("session_id"),
        "started_at": metadata.get("started_at"),
        "started_at_epoch": metadata.get("started_at_epoch"),
        "completed_at": metadata.get("completed_at"),
        "completed_at_epoch": metadata.get("completed_at_epoch"),
        "audio_bytes": metadata.get("audio_bytes", 0),
        "mp3_status": metadata.get("mp3_status"),
        "transcription_status": metadata.get("transcription_status"),
        "summary_status": metadata.get("summary_status"),
        "transcript_correction_count": len(metadata.get("transcript_corrections", [])),
    }


def sessions_for_device(device_id: str) -> list[dict]:
    """Read actual session folders for one device from disk."""
    sessions: list[dict] = []

    metadata_paths = list(device_sessions_root(device_id).glob("session_*/metadata.json"))
    metadata_paths.extend(legacy_sessions_root().glob("session_*/metadata.json"))

    for path in metadata_paths:
        try:
            metadata = json.loads(path.read_text())
        except (OSError, json.JSONDecodeError):
            print("skipping unreadable session metadata:", str(path), flush=True)
            continue

        if metadata.get("device_id") != device_id:
            continue

        sessions.append(metadata)

    return sorted(
        sessions,
        key=lambda metadata: (
            metadata.get("completed_at_epoch") or metadata.get("started_at_epoch") or 0,
            metadata.get("started_at") or "",
            metadata.get("session_id") or "",
        ),
    )


def latest_completed_session(sessions: list[dict]) -> dict | None:
    """Find the newest completed session metadata from a session list."""
    latest: dict | None = None
    latest_epoch = -1

    for metadata in sessions:
        completed_epoch = metadata.get("completed_at_epoch")
        if not isinstance(completed_epoch, int) or completed_epoch <= 0:
            continue

        if completed_epoch > latest_epoch:
            latest = metadata
            latest_epoch = completed_epoch

    return latest


def refresh_state_from_sessions(device_id: str, state: dict) -> dict:
    """Make session folders the ground truth for last-yap fields in state.json."""
    sessions = sessions_for_device(device_id)
    completed_sessions = [
        session
        for session in sessions
        if isinstance(session.get("completed_at_epoch"), int) and session.get("completed_at_epoch") > 0
    ]
    latest = latest_completed_session(sessions)
    previous_epoch = state.get("last_yap_completed_at_epoch")

    state["sessions"] = [session_snapshot(session) for session in sessions]
    state["session_count"] = len(sessions)
    state["completed_session_count"] = len(completed_sessions)

    if latest is None:
        if previous_epoch is not None:
            print("no prior completed sessions found; clearing last yap state:", device_id, flush=True)
        else:
            print("no prior completed sessions found:", device_id, flush=True)
        state.pop("last_yap_completed_at", None)
        state.pop("last_yap_completed_at_epoch", None)
        state.pop("last_session_id", None)
        state["mode"] = "reminder"
        return state

    latest_epoch = latest["completed_at_epoch"]
    state["last_yap_completed_at"] = latest.get("completed_at")
    state["last_yap_completed_at_epoch"] = latest_epoch
    state["last_session_id"] = latest.get("session_id")

    if previous_epoch != latest_epoch:
        print(
            "last yap state refreshed from sessions:",
            device_id,
            f"session={state['last_session_id']}",
            f"epoch={latest_epoch}",
            flush=True,
        )

    return state


def device_status_payload(device_id: str, state: dict) -> dict:
    """Return the device state fields firmware should sync from."""
    return {
        "device_id": device_id,
        "last_seen_at": state.get("last_seen_at"),
        "last_yap_completed_at": state.get("last_yap_completed_at"),
        "last_yap_completed_at_epoch": state.get("last_yap_completed_at_epoch", 0),
        "last_session_id": state.get("last_session_id"),
        "mode": state.get("mode"),
        "session_count": state.get("session_count", 0),
        "completed_session_count": state.get("completed_session_count", 0),
        "server_time": now_iso(),
    }


def session_dir_for_device(device_id: str, session_id: str) -> Path:
    return device_sessions_root(device_id) / session_id


def session_dir(session_id: str) -> Path:
    for path in (storage_root() / "devices").glob("*/sessions/session_*"):
        if path.name == session_id:
            return path
    return legacy_sessions_root() / session_id


def metadata_path(session_id: str, device_id: str | None = None) -> Path:
    folder = session_dir_for_device(device_id, session_id) if device_id is not None else session_dir(session_id)
    return folder / "metadata.json"


def audio_path(session_id: str, device_id: str | None = None) -> Path:
    # Raw little-endian signed 16-bit mono PCM. This is easy for firmware to
    # upload and can later be converted to WAV/MP3 for playback/transcription.
    folder = session_dir_for_device(device_id, session_id) if device_id is not None else session_dir(session_id)
    return folder / "audio.pcm_s16le"


def mp3_path(session_id: str, device_id: str | None = None) -> Path:
    folder = session_dir_for_device(device_id, session_id) if device_id is not None else session_dir(session_id)
    return folder / "audio.mp3"


def run_session_transcription(session_id: str) -> None:
    """Run local speech-to-text and then create the session summary."""
    metadata = read_metadata(session_id)
    if metadata.get("mp3_status") != "ready":
        metadata["transcription_status"] = "skipped"
        metadata["transcription_error"] = "MP3 was not ready"
        write_metadata(session_id, metadata)
        return

    print("transcription started:", session_id, flush=True)
    result = transcribe_mp3(metadata_path(session_id).parent)
    metadata = read_metadata(session_id)
    metadata.update(result)
    metadata["transcription_completed_at"] = now_iso()
    if metadata.get("transcription_status") == "complete":
        metadata["summary_status"] = "processing"
    write_metadata(session_id, metadata)
    print(
        "transcription finished:",
        session_id,
        f"status={metadata.get('transcription_status')}",
        flush=True,
    )
    if metadata.get("transcription_status") != "complete":
        return

    correction_result = correct_transcript(session_id, metadata_path(session_id).parent)
    metadata = read_metadata(session_id)
    metadata.update(correction_result)
    write_metadata(session_id, metadata)
    print("transcript memory corrections:", session_id, f"count={len(correction_result['transcript_corrections'])}", flush=True)

    print("summary started:", session_id, flush=True)
    summary_result = summarize_transcript(metadata_path(session_id).parent)
    metadata = read_metadata(session_id)
    metadata.update(summary_result)
    metadata["summary_completed_at"] = now_iso()
    if metadata.get("summary_status") == "complete":
        transcript = transcript_path(metadata_path(session_id).parent).read_text().strip()
        metadata.update(learn_from_session(session_id, transcript))
    metadata["state"] = "complete"
    write_metadata(session_id, metadata)
    print("summary finished:", session_id, f"status={metadata.get('summary_status')}", flush=True)


def read_metadata(session_id: str) -> dict:
    stored = load_metadata(session_id)
    if stored is not None:
        return stored
    path = metadata_path(session_id)
    if not path.exists():
      raise HTTPException(status_code=404, detail="session not found")
    return json.loads(path.read_text())


def write_metadata(session_id: str, metadata: dict) -> None:
    device_id = metadata.get("device_id")
    if not isinstance(device_id, str) or not device_id:
        raise ValueError("metadata requires device_id")
    existing_path = metadata_path(session_id)
    folder = existing_path.parent if existing_path.exists() else session_dir_for_device(device_id, session_id)
    folder.mkdir(parents=True, exist_ok=True)
    save_metadata(metadata, folder / "metadata.json", now_iso())


def processing_worker() -> None:
    """Process durable queued jobs and recover jobs interrupted by a restart."""
    while True:
        for session_id in claim_jobs(now_iso()):
            try:
                run_session_transcription(session_id)
            except Exception as error:
                finish_job(session_id, now_iso(), str(error)[:2000])
            else:
                metadata = read_metadata(session_id)
                failed = metadata.get("transcription_status") == "failed" or metadata.get("summary_status") == "failed"
                finish_job(session_id, now_iso(), "processing failed; inspect session metadata" if failed else None)
        processing_wakeup.wait(settings.yappl_processing_poll_seconds)
        processing_wakeup.clear()


@app.on_event("startup")
def start_processing_worker() -> None:
    initialize_session_store()
    recover_interrupted_jobs(now_iso())
    if not any(thread.name == "yappl-processing" for thread in threading.enumerate()):
        threading.Thread(target=processing_worker, name="yappl-processing", daemon=True).start()
    processing_wakeup.set()


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


@app.get("/", include_in_schema=False)
def journal_dashboard() -> Response:
    return FileResponse(WEB_ROOT / "index.html", media_type="text/html")


def all_session_metadata() -> list[dict]:
    sessions: list[dict] = []
    devices_root = storage_root() / "devices"
    if not devices_root.exists():
        return sessions
    for device_folder in devices_root.iterdir():
        if device_folder.is_dir():
            sessions.extend(sessions_for_device(device_folder.name))
    return sessions


def session_summary_text(metadata: dict) -> str:
    path = summary_path(metadata_path(metadata["session_id"]).parent)
    return canonicalize_known_aliases(path.read_text().strip()) if path.exists() else ""


def session_transcript_text(metadata: dict) -> str:
    """Return the corrected transcript with the latest known aliases applied."""
    folder = metadata_path(metadata["session_id"]).parent
    corrected = folder / "transcript.corrected.txt"
    path = corrected if corrected.exists() else transcript_path(folder)
    return canonicalize_known_aliases(path.read_text().strip()) if path.exists() else ""


def summary_preview(summary: str, max_characters: int = 240) -> str:
    """Return a clean one-to-two-sentence preview instead of a raw truncation."""
    if not summary:
        return ""
    sentences = re.split(r"(?<=[.!?])\s+", " ".join(summary.split()))
    selected: list[str] = []
    for sentence in sentences:
        candidate = " ".join([*selected, sentence]).strip()
        if selected and len(candidate) > max_characters:
            break
        selected.append(sentence)
        if len(selected) == 2 or len(candidate) >= 120:
            break
    preview = " ".join(selected).strip()
    if len(preview) <= max_characters:
        return preview
    shortened = preview[:max_characters].rsplit(" ", 1)[0].rstrip(" ,;:")
    return shortened + "…"


@app.get("/api/journal/sessions")
def journal_sessions() -> dict:
    """Read-only session index used by the local journal dashboard."""
    items = []
    for metadata in all_session_metadata():
        summary = session_summary_text(metadata)
        transcript = session_transcript_text(metadata)
        sample_rate = int(metadata.get("sample_rate_hz") or 16000)
        duration_seconds = int(metadata.get("audio_bytes", 0)) // max(sample_rate * 2, 1)
        items.append(
            {
                "session_id": metadata.get("session_id"),
                "device_id": metadata.get("device_id"),
                "completed_at": metadata.get("completed_at"),
                "completed_at_epoch": metadata.get("completed_at_epoch"),
                "duration_seconds": duration_seconds,
                "summary_status": metadata.get("summary_status"),
                "summary_excerpt": canonicalize_known_aliases(metadata.get("summary_preview") or summary_preview(summary)),
                "summary": summary,
                "transcript": transcript,
                "audio_url": f"/api/journal/sessions/{metadata.get('session_id')}/audio.mp3" if metadata.get("mp3_status") == "ready" else None,
            }
        )
    items.sort(key=lambda item: item.get("completed_at_epoch") or 0)
    return {"sessions": items}


@app.get("/api/journal/period-summaries")
def journal_period_summaries(period_type: str = Query(...)) -> dict:
    """Return stored weekly/monthly summaries, generating stale completed periods."""
    try:
        items = ensure_period_summaries(period_type, journal_sessions()["sessions"])
    except ValueError as error:
        raise HTTPException(status_code=400, detail=str(error)) from error
    sessions = {item["session_id"]: item for item in journal_sessions()["sessions"]}
    for item in items:
        included = [sessions[session_id] for session_id in item["session_ids"] if session_id in sessions]
        item["session_count"] = len(included)
        item["duration_seconds"] = sum(session["duration_seconds"] for session in included)
        item["summary"] = canonicalize_known_aliases(item.get("summary", ""))
        item["summary_excerpt"] = summary_preview(item["summary"], max_characters=220)
    return {"period_type": period_type, "summaries": items}


@app.get("/api/journal/sessions/{session_id}/audio.mp3")
def journal_session_audio(session_id: str) -> Response:
    """Stream a saved session recording to the local journal dashboard."""
    metadata = read_metadata(session_id)
    path = mp3_path(session_id)
    if metadata.get("mp3_status") != "ready" or not path.exists():
        raise HTTPException(status_code=404, detail="audio not ready")
    return FileResponse(path, media_type="audio/mpeg", filename=f"{session_id}.mp3")


@app.get("/api/memory/library")
def memory_library() -> dict:
    """Read-only categorized memory for the local dashboard."""
    entities = list_entities()
    return {
        "categories": {
            category: [entity for entity in entities if entity["type"] == category]
            for category in ("person", "place", "object", "event", "organization", "project")
        },
        "pending_entities": list_pending_entities(),
    }


@app.get("/memory/entities")
def memory_entities(authorization: str | None = Header(default=None)) -> dict:
    """List canonical people/projects, aliases, and active facts."""
    require_device_secret(authorization)
    return {
        "entities": list_entities(),
        "pending_entities": list_pending_entities(),
    }


@app.put("/memory/entities/{entity_id}")
def update_memory_entity(entity_id: str, payload: MemoryEntityInput, authorization: str | None = Header(default=None)) -> dict:
    """Create or update a canonical entity and its user-confirmed memory."""
    require_device_secret(authorization)
    try:
        entity = upsert_entity(
            entity_id,
            payload.type,
            payload.canonical_name,
            payload.description,
            payload.aliases,
            [fact.model_dump() for fact in payload.facts],
            replace=True,
        )
    except (sqlite3.Error, ValueError) as error:
        raise HTTPException(status_code=400, detail=str(error)) from error
    return {"status": "ok", "entity": entity}


def save_dashboard_memory_entity(payload: MemoryEntityInput, entity_id: str | None = None) -> dict:
    allowed = {"person", "place", "object", "event", "organization", "project"}
    if payload.type not in allowed:
        raise HTTPException(status_code=400, detail="invalid memory category")
    if not payload.canonical_name.strip():
        raise HTTPException(status_code=400, detail="name is required")
    try:
        entity = upsert_entity(
            entity_id,
            payload.type,
            payload.canonical_name.strip(),
            payload.description.strip(),
            [alias.strip() for alias in payload.aliases if alias.strip()],
            [fact.model_dump() for fact in payload.facts if fact.predicate.strip() and fact.value.strip()],
            replace=True,
        )
    except (sqlite3.Error, ValueError) as error:
        raise HTTPException(status_code=400, detail=str(error)) from error
    return {"status": "ok", "entity": entity}


@app.post("/api/memory/entities")
def create_dashboard_memory_entity(payload: MemoryEntityInput) -> dict:
    return save_dashboard_memory_entity(payload)


@app.put("/api/memory/entities/{entity_id}")
def update_dashboard_memory_entity(entity_id: str, payload: MemoryEntityInput) -> dict:
    return save_dashboard_memory_entity(payload, entity_id)


@app.delete("/api/memory/entities/{entity_id}")
def delete_dashboard_memory_entity(entity_id: str) -> dict:
    if not delete_entity(entity_id):
        raise HTTPException(status_code=404, detail="memory not found")
    return {"status": "ok"}


@app.post("/api/memory/pending/{normalized_key}/promote")
def promote_dashboard_pending_memory(normalized_key: str, payload: MemoryEntityInput) -> dict:
    allowed = {"person", "place", "object", "event", "organization", "project"}
    if payload.type not in allowed or not payload.canonical_name.strip():
        raise HTTPException(status_code=400, detail="a valid category and name are required")
    try:
        entity = promote_pending_entity(
            normalized_key,
            payload.type,
            payload.canonical_name.strip(),
            payload.description.strip(),
            [alias.strip() for alias in payload.aliases if alias.strip()],
            [fact.model_dump() for fact in payload.facts if fact.predicate.strip() and fact.value.strip()],
        )
    except KeyError as error:
        raise HTTPException(status_code=404, detail=str(error)) from error
    except (sqlite3.Error, ValueError) as error:
        raise HTTPException(status_code=400, detail=str(error)) from error
    return {"status": "ok", "entity": entity}


@app.get("/settings/summary")
def summary_settings(authorization: str | None = Header(default=None)) -> dict:
    """Return available providers and the active selection without exposing keys."""
    require_device_secret(authorization)
    return {
        "enabled": settings.yappl_summary_enabled,
        "active": public_provider_config(),
        "providers": [
            {"id": provider_id, "label": provider["label"], "default_model": provider["default_model"], "requires_api_key": provider["kind"] != "ollama"}
            for provider_id, provider in PROVIDERS.items()
        ],
    }


@app.put("/settings/summary")
def update_summary_settings(payload: SummarySettingsUpdate, authorization: str | None = Header(default=None)) -> dict:
    """Select a provider/model and optionally encrypt a new API key on disk."""
    require_device_secret(authorization)
    try:
        return {"status": "ok", "active": save_provider_config(payload.provider, payload.model, payload.api_key, payload.base_url)}
    except ValueError as error:
        raise HTTPException(status_code=400, detail=str(error)) from error


@app.post("/settings/summary/test")
def test_summary_settings(authorization: str | None = Header(default=None)) -> dict:
    """Make a tiny real provider request to verify the saved selection and key."""
    require_device_secret(authorization)
    result = summarize_text("I had a short, productive day and finished an important task.")
    if result.get("summary_status") != "complete":
        raise HTTPException(status_code=400, detail=result.get("summary_error", "provider test failed"))
    return {"status": "ok", "provider": result["summary_provider"], "model": result["summary_model"]}


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
        "audio_file": str(audio_path(session_id, payload.device_id)),
        "mp3_status": "not_created",
        "mp3_file": None,
        "mp3_bytes": 0,
        "transcription_status": "pending",
        "transcript_file": None,
        "transcript_bytes": 0,
        "summary_status": "pending",
        "summary_file": None,
        "summary_bytes": 0,
        "state": "recording",
        "last_audio_sequence": 0,
    }
    register_session(metadata, metadata_path(session_id, payload.device_id), now_iso())
    audio_path(session_id, payload.device_id).write_bytes(b"")

    print("session started:", payload.device_id, session_id, flush=True)
    return {
        "status": "ok",
        "session_id": session_id,
        "server_time": now_iso(),
    }


@app.post("/device/session/audio")
async def session_audio(
    session_id: str = Query(...),
    sequence: int = Query(..., ge=1),
    authorization: str | None = Header(default=None),
    body: bytes = Body(...),
) -> dict:
    """Append one raw PCM audio chunk to a session's audio file."""
    require_device_secret(authorization)

    if not body:
        raise HTTPException(status_code=400, detail="empty audio chunk")
    if len(body) > settings.yappl_audio_chunk_max_bytes:
        raise HTTPException(status_code=413, detail="audio chunk too large")
    metadata = read_metadata(session_id)
    # Transparently register legacy file-only sessions in the transactional
    # ledger before accepting their next chunk.
    save_metadata(metadata, metadata_path(session_id), now_iso())
    if int(metadata.get("audio_bytes", 0)) + len(body) > settings.yappl_session_max_bytes:
        raise HTTPException(status_code=413, detail="session audio limit exceeded")
    try:
        metadata, duplicate = append_chunk(
            session_id, sequence, body, audio_path(session_id), metadata, now_iso()
        )
    except KeyError as error:
        raise HTTPException(status_code=404, detail=str(error)) from error
    except RuntimeError as error:
        raise HTTPException(status_code=409, detail=str(error)) from error
    except (ValueError, IndexError) as error:
        raise HTTPException(status_code=409, detail=str(error)) from error
    # Keep the human-readable metadata mirror atomic and synchronized.
    write_metadata(session_id, metadata)

    return {
        "status": "ok",
        "session_id": session_id,
        "received_bytes": len(body),
        "audio_bytes": metadata["audio_bytes"],
        "acknowledged_sequence": sequence,
        "duplicate": duplicate,
    }


@app.post("/device/session/finish")
def session_finish(
    payload: SessionFinish,
    authorization: str | None = Header(default=None),
) -> dict:
    """Mark a durable audio session complete."""
    require_device_secret(authorization)

    metadata = read_metadata(payload.session_id)
    if metadata["device_id"] != payload.device_id:
        raise HTTPException(status_code=400, detail="device/session mismatch")
    if metadata.get("state") == "complete":
        return {
            "status": "ok",
            "session_id": payload.session_id,
            "audio_bytes": metadata["audio_bytes"],
            "mp3_status": metadata.get("mp3_status"),
            "transcription_status": metadata.get("transcription_status"),
            "last_yap_completed_at_epoch": metadata.get("completed_at_epoch"),
            "duplicate": True,
        }
    if metadata.get("state") != "recording":
        raise HTTPException(status_code=409, detail="session cannot be finished from its current state")

    metadata["completed_at"] = now_iso()
    metadata["completed_at_epoch"] = payload.completed_at_epoch
    metadata["state"] = "processing"
    metadata = convert_pcm_to_mp3(payload.session_id, metadata)
    metadata["transcription_status"] = "queued" if metadata.get("mp3_status") == "ready" else "skipped"
    write_metadata(payload.session_id, metadata)

    if metadata["transcription_status"] == "queued":
        enqueue_job(payload.session_id, now_iso())
        processing_wakeup.set()
    else:
        metadata["state"] = "complete"
        write_metadata(payload.session_id, metadata)

    state = read_device_state(payload.device_id)
    state = refresh_state_from_sessions(payload.device_id, state)
    write_device_state(payload.device_id, state)

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
        "transcription_status": metadata.get("transcription_status"),
        "last_yap_completed_at_epoch": payload.completed_at_epoch,
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


@app.get("/device/session/{session_id}/transcript.txt")
def session_transcript_download(session_id: str, authorization: str | None = Header(default=None)) -> Response:
    """Download the local Whisper transcript for a completed session."""
    require_device_secret(authorization)
    metadata = read_metadata(session_id)
    path = transcript_path(metadata_path(session_id).parent)
    if metadata.get("transcription_status") != "complete" or not path.exists():
        raise HTTPException(status_code=404, detail="transcript not ready")
    return FileResponse(path, media_type="text/plain", filename="transcript.txt")


@app.get("/device/session/{session_id}/summary.txt")
def session_summary_download(session_id: str, authorization: str | None = Header(default=None)) -> Response:
    """Download the AI-generated daily summary for a completed session."""
    require_device_secret(authorization)
    metadata = read_metadata(session_id)
    path = summary_path(metadata_path(session_id).parent)
    if metadata.get("summary_status") != "complete" or not path.exists():
        raise HTTPException(status_code=404, detail="summary not ready")
    return FileResponse(path, media_type="text/plain", filename="summary.txt")


@app.post("/device/session/{session_id}/summary/retry")
def retry_session_summary(session_id: str, authorization: str | None = Header(default=None)) -> dict:
    """Retry summary generation without retranscribing the audio."""
    require_device_secret(authorization)
    metadata = read_metadata(session_id)
    if metadata.get("transcription_status") != "complete":
        raise HTTPException(status_code=409, detail="transcript is not complete")
    metadata["summary_status"] = "queued"
    metadata.pop("summary_error", None)
    write_metadata(session_id, metadata)

    metadata["transcription_status"] = "queued"
    enqueue_job(session_id, now_iso())
    processing_wakeup.set()
    return {"status": "ok", "session_id": session_id, "summary_status": "queued"}


@app.post("/device/session/{session_id}/memory/retry")
def retry_session_memory(session_id: str, authorization: str | None = Header(default=None)) -> dict:
    """Retry memory extraction from an existing transcript without other processing."""
    require_device_secret(authorization)
    metadata = read_metadata(session_id)
    if metadata.get("transcription_status") != "complete":
        raise HTTPException(status_code=409, detail="transcript is not complete")
    path = transcript_path(metadata_path(session_id).parent)
    if not path.exists():
        raise HTTPException(status_code=404, detail="transcript not found")

    result = learn_from_session(session_id, path.read_text().strip())
    metadata.update(result)
    metadata["memory_learning_completed_at"] = now_iso()
    write_metadata(session_id, metadata)
    if result.get("memory_learning_status") != "complete":
        raise HTTPException(status_code=502, detail=result.get("memory_learning_error", "memory learning failed"))
    return {"status": "ok", "session_id": session_id, **result}


@app.post("/device/ping")
def device_ping(payload: DevicePing, authorization: str | None = Header(default=None)) -> dict:
    """Called by Yappl so the backend knows the device is online."""
    require_device_secret(authorization)

    state = read_device_state(payload.device_id)
    state["last_seen_at"] = now_iso()
    state["wifi_connected"] = payload.wifi_connected
    state["time_synced"] = payload.time_synced
    state["mode"] = payload.mode
    state = refresh_state_from_sessions(payload.device_id, state)
    write_device_state(payload.device_id, state)

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
        **device_status_payload(payload.device_id, state),
    }


@app.post("/device/yap-completed")
def yap_completed(payload: YapCompleted, authorization: str | None = Header(default=None)) -> dict:
    """Called when the device finishes a yap session."""
    require_device_secret(authorization)

    state = read_device_state(payload.device_id)
    state = refresh_state_from_sessions(payload.device_id, state)
    write_device_state(payload.device_id, state)

    print(
        "yap completed:",
        payload.device_id,
        f"epoch={payload.completed_at_epoch}",
        flush=True,
    )

    return {
        "status": "ok",
        "last_yap_completed_at_epoch": state.get("last_yap_completed_at_epoch"),
        "server_time": now_iso(),
    }


@app.get("/device/status")
def device_status(device_id: str, authorization: str | None = Header(default=None)) -> dict:
    """Returns the backend's current memory of this device."""
    require_device_secret(authorization)

    state = read_device_state(device_id)
    state = refresh_state_from_sessions(device_id, state)
    write_device_state(device_id, state)
    return device_status_payload(device_id, state)
