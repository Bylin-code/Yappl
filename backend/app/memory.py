import json
import re
import sqlite3
import uuid
from datetime import datetime, timezone
from pathlib import Path

from .settings import settings


def _now() -> str:
    return datetime.now(timezone.utc).isoformat()


def _db_path() -> Path:
    path = Path(settings.yappl_storage_dir) / "memory.sqlite3"
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def _connect() -> sqlite3.Connection:
    connection = sqlite3.connect(_db_path())
    connection.row_factory = sqlite3.Row
    connection.execute("PRAGMA foreign_keys = ON")
    return connection


def initialize_memory() -> None:
    with _connect() as db:
        db.executescript(
            """
            CREATE TABLE IF NOT EXISTS entities (
                id TEXT PRIMARY KEY,
                type TEXT NOT NULL,
                canonical_name TEXT NOT NULL UNIQUE COLLATE NOCASE,
                description TEXT NOT NULL DEFAULT '',
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS aliases (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                entity_id TEXT NOT NULL REFERENCES entities(id) ON DELETE CASCADE,
                alias TEXT NOT NULL COLLATE NOCASE,
                pronunciation TEXT NOT NULL DEFAULT '',
                confidence REAL NOT NULL DEFAULT 1.0,
                source TEXT NOT NULL DEFAULT 'user',
                UNIQUE(entity_id, alias)
            );
            CREATE TABLE IF NOT EXISTS facts (
                id TEXT PRIMARY KEY,
                entity_id TEXT NOT NULL REFERENCES entities(id) ON DELETE CASCADE,
                predicate TEXT NOT NULL,
                value TEXT NOT NULL,
                confidence REAL NOT NULL DEFAULT 1.0,
                source_session_id TEXT,
                status TEXT NOT NULL DEFAULT 'active',
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL,
                UNIQUE(entity_id, predicate, value)
            );
            CREATE TABLE IF NOT EXISTS transcript_corrections (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                original TEXT NOT NULL,
                replacement TEXT NOT NULL,
                entity_id TEXT NOT NULL,
                confidence REAL NOT NULL,
                created_at TEXT NOT NULL
            );
            """
        )
    _seed_entity(
        "person_ylang",
        "person",
        "Ylang",
        "Brady's girlfriend.",
        ["Ylah", "Y Lang", "E-lang", "Ilang"],
        [("relationship_to_user", "girlfriend")],
    )
    _seed_entity(
        "project_yappl",
        "project",
        "Yappl",
        "Brady's private spoken-journal device and software project.",
        ["Yapple", "Yappler"],
        [("owned_by_user", "true")],
    )


def _seed_entity(entity_id: str, entity_type: str, name: str, description: str, aliases: list[str], facts: list[tuple[str, str]]) -> None:
    now = _now()
    with _connect() as db:
        db.execute(
            "INSERT OR IGNORE INTO entities(id,type,canonical_name,description,created_at,updated_at) VALUES(?,?,?,?,?,?)",
            (entity_id, entity_type, name, description, now, now),
        )
        for alias in [name, *aliases]:
            db.execute(
                "INSERT OR IGNORE INTO aliases(entity_id,alias,confidence,source) VALUES(?,?,?,?)",
                (entity_id, alias, 1.0, "user"),
            )
        for predicate, value in facts:
            db.execute(
                "INSERT OR IGNORE INTO facts(id,entity_id,predicate,value,confidence,created_at,updated_at) VALUES(?,?,?,?,?,?,?)",
                (f"fact_{uuid.uuid4().hex}", entity_id, predicate, value, 1.0, now, now),
            )


def list_entities() -> list[dict]:
    initialize_memory()
    with _connect() as db:
        entities = []
        for row in db.execute("SELECT * FROM entities ORDER BY type, canonical_name"):
            item = dict(row)
            item["aliases"] = [dict(value) for value in db.execute("SELECT alias,pronunciation,confidence,source FROM aliases WHERE entity_id=? ORDER BY alias", (row["id"],))]
            item["facts"] = [dict(value) for value in db.execute("SELECT id,predicate,value,confidence,source_session_id,status FROM facts WHERE entity_id=? AND status='active' ORDER BY updated_at DESC", (row["id"],))]
            entities.append(item)
        return entities


def upsert_entity(entity_id: str | None, entity_type: str, name: str, description: str, aliases: list[str], facts: list[dict]) -> dict:
    initialize_memory()
    entity_id = entity_id or f"{entity_type}_{uuid.uuid4().hex}"
    now = _now()
    with _connect() as db:
        db.execute(
            "INSERT INTO entities(id,type,canonical_name,description,created_at,updated_at) VALUES(?,?,?,?,?,?) "
            "ON CONFLICT(id) DO UPDATE SET type=excluded.type,canonical_name=excluded.canonical_name,description=excluded.description,updated_at=excluded.updated_at",
            (entity_id, entity_type, name, description, now, now),
        )
        for alias in [name, *aliases]:
            db.execute("INSERT OR IGNORE INTO aliases(entity_id,alias,confidence,source) VALUES(?,?,1.0,'user')", (entity_id, alias))
        for fact in facts:
            db.execute(
                "INSERT OR REPLACE INTO facts(id,entity_id,predicate,value,confidence,source_session_id,status,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?)",
                (fact.get("id") or f"fact_{uuid.uuid4().hex}", entity_id, fact["predicate"], fact["value"], float(fact.get("confidence", 1.0)), fact.get("source_session_id"), fact.get("status", "active"), now, now),
            )
    return next(item for item in list_entities() if item["id"] == entity_id)


def correct_transcript(session_id: str, session_folder: Path) -> dict:
    """Apply only user-curated alias corrections and preserve the raw transcript."""
    initialize_memory()
    source = session_folder / "transcript.txt"
    raw_path = session_folder / "transcript.raw.txt"
    corrected_path = session_folder / "transcript.corrected.txt"
    raw = source.read_text().strip()
    raw_path.write_text(raw + "\n" if raw else "")
    corrected = raw
    changes: list[dict] = []
    with _connect() as db:
        rows = db.execute(
            "SELECT a.alias,a.confidence,e.id,e.canonical_name FROM aliases a JOIN entities e ON e.id=a.entity_id "
            "WHERE lower(a.alias) != lower(e.canonical_name) ORDER BY length(a.alias) DESC"
        ).fetchall()
        for row in rows:
            pattern = re.compile(rf"(?<!\w){re.escape(row['alias'])}(?!\w)", re.IGNORECASE)
            matches = pattern.findall(corrected)
            if not matches:
                continue
            corrected = pattern.sub(row["canonical_name"], corrected)
            for original in matches:
                change = {"original": original, "replacement": row["canonical_name"], "entity_id": row["id"], "confidence": row["confidence"]}
                changes.append(change)
                db.execute(
                    "INSERT INTO transcript_corrections(session_id,original,replacement,entity_id,confidence,created_at) VALUES(?,?,?,?,?,?)",
                    (session_id, original, row["canonical_name"], row["id"], row["confidence"], _now()),
                )
    corrected_path.write_text(corrected + "\n" if corrected else "")
    source.write_text(corrected + "\n" if corrected else "")
    return {
        "raw_transcript_file": str(raw_path),
        "corrected_transcript_file": str(corrected_path),
        "transcript_corrections": changes,
    }


def relevant_memory_context(transcript: str) -> str:
    initialize_memory()
    lowered = transcript.lower()
    relevant = []
    for entity in list_entities():
        names = [entity["canonical_name"], *[alias["alias"] for alias in entity["aliases"]]]
        if not any(re.search(rf"(?<!\w){re.escape(name.lower())}(?!\w)", lowered) for name in names):
            continue
        facts = "; ".join(f"{fact['predicate'].replace('_', ' ')}: {fact['value']}" for fact in entity["facts"][:8])
        detail = f"- {entity['canonical_name']} ({entity['type']}): {entity['description']}"
        if facts:
            detail += f" Known facts: {facts}."
        relevant.append(detail)
    if not relevant:
        return ""
    return "Known personal context (use only to disambiguate; do not force it into the entry):\n" + "\n".join(relevant)


def learn_from_session(session_id: str, transcript: str) -> dict:
    """Extract explicit, durable facts about known entities with session provenance."""
    from .summarization import generate_text

    entities = list_entities()
    known = "\n".join(f"- {item['canonical_name']} [{item['id']}]: {item['description']}" for item in entities)
    prompt = """Extract only explicit, durable personal facts from this private journal transcript.
Return strict JSON with this shape:
{"facts":[{"entity_id":"...","predicate":"short_snake_case","value":"..."}],
"new_entities":[{"type":"person|project|organization","canonical_name":"...","description":"...","aliases":[],
"facts":[{"predicate":"...","value":"..."}]}]}.
Use supplied entity IDs for known entities. Create a new entity only when the transcript clearly identifies a
durable relationship, recurring role, owned project, or important organization. Never create entities for a
one-off stranger, restaurant, store, ordinary location, meal, or uncertain/misheard name. Do not infer emotions,
personality traits, diagnoses, temporary moods, or facts not directly stated. Do not save ordinary one-day events.
Good memories include relationships, occupations, ownership of projects, stable preferences, and durable
biographical context. If there is nothing durable, return {"facts":[],"new_entities":[]}. Return JSON only."""
    try:
        text, provider, model = generate_text(f"{prompt}\n\nKnown entities:\n{known}", transcript, max_tokens=500)
        cleaned = re.sub(r"^```(?:json)?\s*|\s*```$", "", text.strip(), flags=re.IGNORECASE)
        payload = json.loads(cleaned)
    except Exception as error:
        return {"memory_learning_status": "failed", "memory_learning_error": str(error)}

    valid_ids = {item["id"] for item in entities}
    learned = []
    now = _now()
    with _connect() as db:
        for fact in payload.get("facts", [])[:20]:
            entity_id = fact.get("entity_id")
            predicate = str(fact.get("predicate", "")).strip()[:80]
            value = str(fact.get("value", "")).strip()[:500]
            if entity_id not in valid_ids or not predicate or not value:
                continue
            fact_id = f"fact_{uuid.uuid4().hex}"
            db.execute(
                "INSERT OR IGNORE INTO facts(id,entity_id,predicate,value,confidence,source_session_id,status,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?)",
                (fact_id, entity_id, predicate, value, 0.9, session_id, "active", now, now),
            )
            if db.execute("SELECT changes()").fetchone()[0]:
                learned.append({"entity_id": entity_id, "predicate": predicate, "value": value})
    new_entities = []
    for item in payload.get("new_entities", [])[:10]:
        entity_type = str(item.get("type", "")).strip().lower()
        name = str(item.get("canonical_name", "")).strip()[:120]
        description = str(item.get("description", "")).strip()[:500]
        if entity_type not in {"person", "project", "organization"} or not name or not description:
            continue
        try:
            entity = upsert_entity(
                None,
                entity_type,
                name,
                description,
                [str(alias).strip()[:120] for alias in item.get("aliases", []) if str(alias).strip()][:10],
                [
                    {
                        "predicate": str(fact.get("predicate", "")).strip()[:80],
                        "value": str(fact.get("value", "")).strip()[:500],
                        "confidence": 0.85,
                        "source_session_id": session_id,
                    }
                    for fact in item.get("facts", [])[:10]
                    if str(fact.get("predicate", "")).strip() and str(fact.get("value", "")).strip()
                ],
            )
            new_entities.append({"id": entity["id"], "canonical_name": entity["canonical_name"], "type": entity["type"]})
        except sqlite3.IntegrityError:
            continue
    return {"memory_learning_status": "complete", "memory_facts_learned": learned, "memory_entities_learned": new_entities, "memory_provider": provider, "memory_model": model}
