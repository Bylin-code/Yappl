import hashlib
import json
import os
import sqlite3
import threading
from pathlib import Path

from .settings import settings


_schema_lock = threading.Lock()
_session_locks: dict[str, threading.Lock] = {}
_session_locks_guard = threading.Lock()


def database_path() -> Path:
    root = Path(settings.yappl_storage_dir)
    root.mkdir(parents=True, exist_ok=True)
    return root / "sessions.sqlite3"


def connect() -> sqlite3.Connection:
    db = sqlite3.connect(database_path(), timeout=30)
    db.row_factory = sqlite3.Row
    db.execute("PRAGMA journal_mode=WAL")
    db.execute("PRAGMA foreign_keys=ON")
    return db


def initialize() -> None:
    with _schema_lock, connect() as db:
        db.executescript(
            """
            CREATE TABLE IF NOT EXISTS sessions (
                session_id TEXT PRIMARY KEY,
                device_id TEXT NOT NULL,
                state TEXT NOT NULL,
                metadata_json TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS audio_chunks (
                session_id TEXT NOT NULL REFERENCES sessions(session_id) ON DELETE CASCADE,
                sequence INTEGER NOT NULL,
                byte_count INTEGER NOT NULL,
                sha256 TEXT NOT NULL,
                PRIMARY KEY(session_id, sequence)
            );
            CREATE TABLE IF NOT EXISTS processing_jobs (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL UNIQUE REFERENCES sessions(session_id) ON DELETE CASCADE,
                status TEXT NOT NULL DEFAULT 'queued',
                attempts INTEGER NOT NULL DEFAULT 0,
                last_error TEXT,
                updated_at TEXT NOT NULL
            );
            """
        )


def session_lock(session_id: str) -> threading.Lock:
    with _session_locks_guard:
        return _session_locks.setdefault(session_id, threading.Lock())


def atomic_write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True))
    os.replace(temporary, path)


def register_session(metadata: dict, metadata_path: Path, now: str) -> None:
    initialize()
    atomic_write_json(metadata_path, metadata)
    with connect() as db:
        db.execute(
            "INSERT INTO sessions(session_id,device_id,state,metadata_json,updated_at) VALUES(?,?,?,?,?)",
            (metadata["session_id"], metadata["device_id"], metadata["state"], json.dumps(metadata), now),
        )


def save_metadata(metadata: dict, metadata_path: Path, now: str) -> None:
    initialize()
    atomic_write_json(metadata_path, metadata)
    with connect() as db:
        db.execute(
            "INSERT INTO sessions(session_id,device_id,state,metadata_json,updated_at) VALUES(?,?,?,?,?) "
            "ON CONFLICT(session_id) DO UPDATE SET device_id=excluded.device_id,state=excluded.state,"
            "metadata_json=excluded.metadata_json,updated_at=excluded.updated_at",
            (metadata["session_id"], metadata["device_id"], metadata.get("state", "recording"), json.dumps(metadata), now),
        )


def load_metadata(session_id: str) -> dict | None:
    initialize()
    with connect() as db:
        row = db.execute("SELECT metadata_json FROM sessions WHERE session_id=?", (session_id,)).fetchone()
    return json.loads(row["metadata_json"]) if row else None


def append_chunk(session_id: str, sequence: int, body: bytes, audio_path: Path, metadata: dict, now: str) -> tuple[dict, bool]:
    """Append exactly once. A retry with identical bytes returns success without appending."""
    initialize()
    digest = hashlib.sha256(body).hexdigest()
    with session_lock(session_id), connect() as db:
        db.execute("BEGIN IMMEDIATE")
        row = db.execute("SELECT state FROM sessions WHERE session_id=?", (session_id,)).fetchone()
        if row is None:
            raise KeyError("session not found")
        if row["state"] != "recording":
            raise RuntimeError("session is not accepting audio")

        existing = db.execute(
            "SELECT byte_count,sha256 FROM audio_chunks WHERE session_id=? AND sequence=?",
            (session_id, sequence),
        ).fetchone()
        if existing is not None:
            if existing["byte_count"] != len(body) or existing["sha256"] != digest:
                raise ValueError("sequence already exists with different audio")
            db.commit()
            return metadata, True

        expected = db.execute(
            "SELECT COALESCE(MAX(sequence),0)+1 AS value FROM audio_chunks WHERE session_id=?",
            (session_id,),
        ).fetchone()["value"]
        if sequence != expected:
            raise IndexError(f"expected audio sequence {expected}")

        with audio_path.open("ab") as audio_file:
            audio_file.write(body)
            audio_file.flush()
            os.fsync(audio_file.fileno())

        metadata["audio_bytes"] = int(metadata.get("audio_bytes", 0)) + len(body)
        metadata["last_audio_chunk_at"] = now
        metadata["last_audio_sequence"] = sequence
        db.execute(
            "INSERT INTO audio_chunks(session_id,sequence,byte_count,sha256) VALUES(?,?,?,?)",
            (session_id, sequence, len(body), digest),
        )
        db.execute(
            "UPDATE sessions SET metadata_json=?,updated_at=? WHERE session_id=?",
            (json.dumps(metadata), now, session_id),
        )
        db.commit()
        return metadata, False


def enqueue_job(session_id: str, now: str) -> None:
    initialize()
    with connect() as db:
        db.execute(
            "INSERT INTO processing_jobs(session_id,status,attempts,updated_at) VALUES(?,'queued',0,?) "
            "ON CONFLICT(session_id) DO UPDATE SET status='queued',last_error=NULL,updated_at=excluded.updated_at",
            (session_id, now),
        )


def claim_jobs(now: str, limit: int = 4) -> list[str]:
    initialize()
    with connect() as db:
        db.execute("BEGIN IMMEDIATE")
        rows = db.execute(
            "SELECT session_id FROM processing_jobs WHERE status='queued' ORDER BY id LIMIT ?", (limit,)
        ).fetchall()
        ids = [row["session_id"] for row in rows]
        for session_id in ids:
            db.execute(
                "UPDATE processing_jobs SET status='processing',attempts=attempts+1,updated_at=? WHERE session_id=?",
                (now, session_id),
            )
        db.commit()
        return ids


def recover_interrupted_jobs(now: str) -> None:
    """Requeue work that was processing when the previous process stopped."""
    initialize()
    with connect() as db:
        db.execute(
            "UPDATE processing_jobs SET status='queued',updated_at=? WHERE status='processing'",
            (now,),
        )


def finish_job(session_id: str, now: str, error: str | None = None) -> None:
    with connect() as db:
        db.execute(
            "UPDATE processing_jobs SET status=?,last_error=?,updated_at=? WHERE session_id=?",
            ("failed" if error else "complete", error, now, session_id),
        )
