import hashlib
import json
import re
import sqlite3
from calendar import monthrange
from datetime import date, datetime, timedelta
from pathlib import Path
from zoneinfo import ZoneInfo

from .memory import canonicalize_known_aliases
from .settings import settings
from .summarization import generate_text


LOCAL_TIME_ZONE = ZoneInfo("America/Los_Angeles")
SUMMARY_POLICY_VERSION = 2
SUMMARY_LENGTHS = {
    "weekly": (120, 300, 0.25),
    "monthly": (300, 600, 0.20),
    "yearly": (700, 1200, 0.15),
}


def _db_path() -> Path:
    return Path(settings.yappl_storage_dir) / "period_summaries.sqlite3"


def _connect() -> sqlite3.Connection:
    connection = sqlite3.connect(_db_path())
    connection.row_factory = sqlite3.Row
    return connection


def initialize() -> None:
    _db_path().parent.mkdir(parents=True, exist_ok=True)
    with _connect() as db:
        db.execute(
            """CREATE TABLE IF NOT EXISTS period_summaries (
                id TEXT PRIMARY KEY,
                period_type TEXT NOT NULL,
                period_start TEXT NOT NULL,
                period_end TEXT NOT NULL,
                summary TEXT NOT NULL DEFAULT '',
                session_ids_json TEXT NOT NULL,
                source_hash TEXT NOT NULL,
                status TEXT NOT NULL,
                error TEXT,
                provider TEXT,
                model TEXT,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            )"""
        )


def journal_date(epoch: int) -> date:
    """Return the user's journal date, whose local day begins at 8 AM."""
    return (datetime.fromtimestamp(epoch, LOCAL_TIME_ZONE) - timedelta(hours=8)).date()


def _period_for(day: date, period_type: str) -> tuple[date, date]:
    if period_type == "weekly":
        start = day - timedelta(days=day.weekday())
        return start, start + timedelta(days=6)
    if period_type == "monthly":
        start = day.replace(day=1)
        return start, start.replace(day=monthrange(start.year, start.month)[1])
    return date(day.year, 1, 1), date(day.year, 12, 31)


def _source_hash(sessions: list[dict]) -> str:
    source = [
        {
            "id": item["session_id"],
            "completed": item.get("completed_at_epoch"),
            "summary": item.get("summary", ""),
        }
        for item in sessions
    ]
    return hashlib.sha256(json.dumps({"policy": SUMMARY_POLICY_VERSION, "sessions": source}, sort_keys=True, ensure_ascii=False).encode()).hexdigest()


def summary_target_words(period_type: str, source: str) -> int:
    minimum, maximum, ratio = SUMMARY_LENGTHS[period_type]
    return max(minimum, min(maximum, round(len(source.split()) * ratio)))


def _clean_summary(value: str) -> str:
    return re.sub(r"^\s*#{1,6}[^\n]*\n+", "", value).strip()


def _write_markdown(item: dict) -> None:
    folder = Path(settings.yappl_storage_dir) / "summaries" / item["period_type"]
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / f"{item['period_start']}.md"
    path.write_text(
        f"# {item['period_type'].title()} summary: {item['period_start']} to {item['period_end']}\n\n"
        f"{item['summary'].strip()}\n\n"
        f"## Included sessions\n\n"
        + "\n".join(f"- `{session_id}`" for session_id in item["session_ids"])
        + "\n"
    )


def _generate(period_type: str, start: date, end: date, sessions: list[dict], digest: str) -> dict:
    label = {"weekly": "week", "monthly": "month", "yearly": "year"}[period_type]
    entries = []
    for session in sessions:
        entries.append(f"[{journal_date(int(session['completed_at_epoch'])).isoformat()}]\n{session.get('summary', '').strip()}")
    source = "\n\n".join(entries)
    target_words = summary_target_words(period_type, source)
    prompt = (
        f"Create a cohesive private-journal summary of this completed {label} ({start} through {end}). "
        "Synthesize the important activities, people, progress, changes, and recurring themes. Preserve chronology "
        "where useful. Do not invent details, give advice, or mention that the input consists of summaries. "
        f"Use natural first-person journal prose and concise paragraphs. Aim for about {target_words} words; "
        "be shorter rather than repetitive when the source material is sparse."
    )
    now = datetime.now(LOCAL_TIME_ZONE).isoformat()
    period_id = f"{period_type}:{start.isoformat()}"
    try:
        summary, provider, model = generate_text(prompt, source, max_tokens=round(target_words * 1.8) + 200)
        summary = _clean_summary(summary)
        status, error = "complete", None
    except Exception as exc:
        summary, provider, model, status, error = "", None, None, "failed", str(exc)
    with _connect() as db:
        db.execute(
            """INSERT INTO period_summaries(id,period_type,period_start,period_end,summary,session_ids_json,source_hash,status,error,provider,model,created_at,updated_at)
               VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)
               ON CONFLICT(id) DO UPDATE SET period_end=excluded.period_end,summary=excluded.summary,
               session_ids_json=excluded.session_ids_json,source_hash=excluded.source_hash,status=excluded.status,
               error=excluded.error,provider=excluded.provider,model=excluded.model,updated_at=excluded.updated_at""",
            (period_id, period_type, start.isoformat(), end.isoformat(), summary, json.dumps([s["session_id"] for s in sessions]), digest, status, error, provider, model, now, now),
        )
    item = {
        "id": period_id, "period_type": period_type, "period_start": start.isoformat(), "period_end": end.isoformat(),
        "summary": summary, "session_ids": [s["session_id"] for s in sessions], "source_hash": digest,
        "status": status, "error": error, "provider": provider, "model": model,
    }
    if status == "complete":
        _write_markdown(item)
    return item


def ensure_period_summaries(period_type: str, sessions: list[dict]) -> list[dict]:
    if period_type not in {"weekly", "monthly", "yearly"}:
        raise ValueError("period type must be weekly, monthly, or yearly")
    initialize()
    grouped: dict[tuple[date, date], list[dict]] = {}
    for session in sessions:
        epoch = session.get("completed_at_epoch")
        if not epoch or not session.get("summary"):
            continue
        period = _period_for(journal_date(int(epoch)), period_type)
        grouped.setdefault(period, []).append(session)
    current_journal_day = journal_date(int(datetime.now(LOCAL_TIME_ZONE).timestamp()))
    results = []
    with _connect() as db:
        stored = {row["id"]: dict(row) for row in db.execute("SELECT * FROM period_summaries WHERE period_type=?", (period_type,))}
    for (start, end), items in sorted(grouped.items(), reverse=True):
        if end >= current_journal_day:
            continue
        items.sort(key=lambda item: item.get("completed_at_epoch") or 0)
        digest = _source_hash(items)
        period_id = f"{period_type}:{start.isoformat()}"
        existing = stored.get(period_id)
        if existing is None or existing["source_hash"] != digest or existing["status"] != "complete":
            results.append(_generate(period_type, start, end, items, digest))
            continue
        existing["session_ids"] = json.loads(existing.pop("session_ids_json"))
        cleaned = canonicalize_known_aliases(_clean_summary(existing["summary"]))
        if cleaned != existing["summary"]:
            existing["summary"] = cleaned
            with _connect() as db:
                db.execute("UPDATE period_summaries SET summary=? WHERE id=?", (cleaned, period_id))
            _write_markdown(existing)
        results.append(existing)
    return results
