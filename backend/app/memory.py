import json
import re
import sqlite3
import uuid
from difflib import SequenceMatcher
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
            CREATE TABLE IF NOT EXISTS pending_entities (
                normalized_key TEXT PRIMARY KEY,
                type TEXT NOT NULL,
                canonical_name TEXT NOT NULL,
                description TEXT NOT NULL DEFAULT '',
                aliases_json TEXT NOT NULL DEFAULT '[]',
                facts_json TEXT NOT NULL DEFAULT '[]',
                first_session_id TEXT NOT NULL,
                last_session_id TEXT NOT NULL,
                mention_count INTEGER NOT NULL DEFAULT 1,
                updated_at TEXT NOT NULL
            );
            DROP TABLE IF EXISTS personal_attributes;
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


def list_pending_entities() -> list[dict]:
    initialize_memory()
    with _connect() as db:
        return [dict(row) for row in db.execute("SELECT * FROM pending_entities ORDER BY type,canonical_name")]


def canonicalize_known_aliases(text: str) -> str:
    """Apply trusted current aliases to any stored/displayed journal text."""
    if not text:
        return text
    initialize_memory()
    corrected = text
    with _connect() as db:
        rows = db.execute(
            "SELECT a.alias,e.canonical_name FROM aliases a JOIN entities e ON e.id=a.entity_id "
            "WHERE lower(a.alias)!=lower(e.canonical_name) ORDER BY length(a.alias) DESC"
        ).fetchall()
    for row in rows:
        corrected = re.sub(
            rf"(?<!\w){re.escape(row['alias'])}(?!\w)",
            row["canonical_name"],
            corrected,
            flags=re.IGNORECASE,
        )
    return corrected


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
    entity = next(item for item in list_entities() if item["id"] == entity_id)
    from .memory_files import sync_memory_files

    sync_memory_files()
    return entity


def _stage_or_promote_entity(session_id: str, item: dict) -> dict | None:
    """Promote an entity only after it is identified in two different sessions."""
    entity_type = str(item.get("type", "")).strip().lower()
    name = str(item.get("canonical_name", "")).strip()[:120]
    description = str(item.get("description", "")).strip()[:500]
    if entity_type not in {"person", "place", "object", "event", "project", "organization"} or not name or not description:
        return None
    normalized_key = f"{entity_type}:{_normalized_name(name)}"
    aliases = [str(alias).strip()[:120] for alias in item.get("aliases", []) if str(alias).strip()][:10]
    facts = [
        {
            "predicate": str(fact.get("predicate", "")).strip()[:80],
            "value": str(fact.get("value", "")).strip()[:500],
            "confidence": 0.85,
            "source_session_id": session_id,
        }
        for fact in item.get("facts", [])[:10]
        if str(fact.get("predicate", "")).strip() and str(fact.get("value", "")).strip()
    ]
    now = _now()
    with _connect() as db:
        existing = db.execute("SELECT * FROM pending_entities WHERE normalized_key=?", (normalized_key,)).fetchone()
        if existing is None:
            db.execute(
                "INSERT INTO pending_entities(normalized_key,type,canonical_name,description,aliases_json,facts_json,first_session_id,last_session_id,mention_count,updated_at) VALUES(?,?,?,?,?,?,?,?,1,?)",
                (normalized_key, entity_type, name, description, json.dumps(aliases), json.dumps(facts), session_id, session_id, now),
            )
            return None
        count = int(existing["mention_count"])
        if existing["last_session_id"] != session_id:
            count += 1
        combined_aliases = list(dict.fromkeys([*json.loads(existing["aliases_json"]), *aliases]))[:20]
        combined_facts = [*json.loads(existing["facts_json"]), *facts][-20:]
        db.execute(
            "UPDATE pending_entities SET description=?,aliases_json=?,facts_json=?,last_session_id=?,mention_count=?,updated_at=? WHERE normalized_key=?",
            (description, json.dumps(combined_aliases), json.dumps(combined_facts), session_id, count, now, normalized_key),
        )
        if count < 2:
            return None
        db.execute("DELETE FROM pending_entities WHERE normalized_key=?", (normalized_key,))
    try:
        return upsert_entity(None, entity_type, name, description, combined_aliases, combined_facts)
    except sqlite3.IntegrityError:
        return None


def _normalized_name(value: str) -> str:
    return re.sub(r"[^a-z0-9]", "", value.lower())


def _local_alias_candidates(transcript: str) -> list[str]:
    """Return name-like tokens while avoiding ordinary sentence-start words."""
    ignored = {"Today", "Tonight", "Tomorrow", "Yesterday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"}
    candidates = re.findall(r"(?<![\w'-])(?:[A-Z][A-Za-z'-]{3,})(?![\w'-])", transcript)
    return list(dict.fromkeys(value for value in candidates if value not in ignored))


def discover_high_confidence_aliases(session_id: str, transcript: str) -> list[dict]:
    """Persist only unique, near-exact local matches; ambiguous names are left alone."""
    initialize_memory()
    learned: list[dict] = []
    entities = list_entities()
    known_names = {
        _normalized_name(name)
        for entity in entities
        for name in [entity["canonical_name"], *[alias["alias"] for alias in entity["aliases"]]]
    }
    with _connect() as db:
        for candidate in _local_alias_candidates(transcript):
            normalized = _normalized_name(candidate)
            if len(normalized) < 5 or normalized in known_names:
                continue
            scored = sorted(
                [
                    (
                        SequenceMatcher(None, normalized, _normalized_name(entity["canonical_name"])).ratio(),
                        entity,
                    )
                    for entity in entities
                ],
                key=lambda item: item[0],
            )
            if not scored:
                continue
            best_score, best = scored[-1]
            runner_up = scored[-2][0] if len(scored) > 1 else 0.0
            # A one-character transcription variation in a five-letter name
            # scores .80. The uniqueness margin prevents merging
            # when two known names are similarly spelled.
            if best_score < 0.80 or best_score - runner_up < 0.08:
                continue
            conflict = db.execute(
                "SELECT entity_id FROM aliases WHERE alias=? COLLATE NOCASE AND entity_id!=? LIMIT 1",
                (candidate, best["id"]),
            ).fetchone()
            if conflict:
                continue
            db.execute(
                "INSERT OR IGNORE INTO aliases(entity_id,alias,confidence,source) VALUES(?,?,?,?)",
                (best["id"], candidate, best_score, f"local:{session_id}"),
            )
            if db.execute("SELECT changes()").fetchone()[0]:
                learned.append(
                    {"entity_id": best["id"], "alias": candidate, "canonical_name": best["canonical_name"], "confidence": round(best_score, 3), "source": "local"}
                )
                known_names.add(normalized)
    return learned


def correct_transcript(session_id: str, session_folder: Path) -> dict:
    """Apply curated and uniquely high-confidence local aliases, preserving raw text."""
    initialize_memory()
    source = session_folder / "transcript.txt"
    raw_path = session_folder / "transcript.raw.txt"
    corrected_path = session_folder / "transcript.corrected.txt"
    raw = source.read_text().strip()
    raw_path.write_text(raw + "\n" if raw else "")
    aliases_learned = discover_high_confidence_aliases(session_id, raw)
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
    from .memory_files import sync_memory_files

    sync_memory_files()
    return {
        "raw_transcript_file": str(raw_path),
        "corrected_transcript_file": str(corrected_path),
        "transcript_corrections": changes,
        "memory_local_aliases_learned": aliases_learned,
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
    # The model receives one compact, authoritative SQLite snapshot. Markdown
    # files are human-readable mirrors and are intentionally not parsed back.
    memory_snapshot = {
        "entities": [
            {
                "id": item["id"],
                "type": item["type"],
                "name": item["canonical_name"],
                "description": item["description"],
                "aliases": [alias["alias"] for alias in item["aliases"]],
                "facts": [
                    {"predicate": fact["predicate"], "value": fact["value"], "status": fact["status"]}
                    for fact in item["facts"]
                ],
            }
            for item in entities
        ],
        "pending_entities": [
            {
                "type": item["type"],
                "name": item["canonical_name"],
                "description": item["description"],
                "mention_count": item["mention_count"],
            }
            for item in list_pending_entities()
        ],
    }
    known = json.dumps(memory_snapshot, separators=(",", ":"), ensure_ascii=False)
    prompt = """Extract only explicit, durable personal facts from this private journal transcript.
Return strict JSON with this shape:
{"facts":[{"entity_id":"...","predicate":"short_snake_case","value":"...","replace_existing":false}],
"aliases":[{"entity_id":"...","alias":"exact transcript spelling","confidence":0.0}],
"new_entities":[{"type":"person|place|object|event|project|organization","canonical_name":"...","description":"...","aliases":[],
"facts":[{"predicate":"...","value":"..."}]}]}.
Suggest an alias for a known entity only when the exact spelling occurs in the transcript, context clearly identifies
the same entity, and confidence is at least 0.92. Never suggest an ordinary word or another real person's name.
Use supplied entity IDs for known entities. Create a new entity only when the transcript clearly identifies a
durable relationship, recurring role, owned project, important organization, personally important place or object,
or a significant event likely to be mentioned again. New entities are staged until they recur in another session.
Never create entities for a one-off stranger, restaurant, store, ordinary location, meal, or uncertain/misheard name.
Review the entire supplied current-memory snapshot against the new transcript. Preserve facts and attributes unless
the transcript explicitly adds, changes, or contradicts them. Set replace_existing=true only when the transcript
clearly replaces an older value for the same predicate; otherwise leave it false. Do not infer emotions,
personality traits, diagnoses, temporary moods, or facts not directly stated. Do not save ordinary one-day events.
Good memories include relationships, occupations, ownership of projects, stable preferences, and durable
biographical context. If there is nothing durable, return {"facts":[],"aliases":[],"new_entities":[]}. Return JSON only."""
    try:
        # Reasoning-capable models may spend part of this allowance on an
        # internal thinking block before emitting the JSON text response.
        text, provider, model = generate_text(f"{prompt}\n\nCurrent memory snapshot (JSON):\n{known}", transcript, max_tokens=3000)
        cleaned = re.sub(r"^```(?:json)?\s*|\s*```$", "", text.strip(), flags=re.IGNORECASE)
        payload = json.loads(cleaned)
    except Exception as error:
        return {"memory_learning_status": "failed", "memory_learning_error": str(error)}

    valid_ids = {item["id"] for item in entities}
    learned = []
    learned_aliases = []
    now = _now()
    with _connect() as db:
        for fact in payload.get("facts", [])[:20]:
            entity_id = fact.get("entity_id")
            predicate = str(fact.get("predicate", "")).strip()[:80]
            value = str(fact.get("value", "")).strip()[:500]
            if entity_id not in valid_ids or not predicate or not value:
                continue
            replace_existing = fact.get("replace_existing") is True
            if replace_existing:
                db.execute(
                    "UPDATE facts SET status='superseded',updated_at=? WHERE entity_id=? AND predicate=? AND value!=? AND status='active'",
                    (now, entity_id, predicate, value),
                )
            fact_id = f"fact_{uuid.uuid4().hex}"
            db.execute(
                "INSERT OR IGNORE INTO facts(id,entity_id,predicate,value,confidence,source_session_id,status,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?)",
                (fact_id, entity_id, predicate, value, 0.9, session_id, "active", now, now),
            )
            db.execute(
                "UPDATE facts SET status='active',confidence=0.9,source_session_id=?,updated_at=? WHERE entity_id=? AND predicate=? AND value=?",
                (session_id, now, entity_id, predicate, value),
            )
            if db.execute("SELECT changes()").fetchone()[0]:
                learned.append({"entity_id": entity_id, "predicate": predicate, "value": value})
        for suggestion in payload.get("aliases", [])[:20]:
            entity_id = suggestion.get("entity_id")
            alias = str(suggestion.get("alias", "")).strip()[:120]
            try:
                confidence = float(suggestion.get("confidence", 0.0))
            except (TypeError, ValueError):
                continue
            if entity_id not in valid_ids or len(alias) < 3 or confidence < 0.92:
                continue
            if re.search(rf"(?<!\w){re.escape(alias)}(?!\w)", transcript, re.IGNORECASE) is None:
                continue
            conflict = db.execute(
                "SELECT entity_id FROM aliases WHERE alias=? COLLATE NOCASE AND entity_id!=? LIMIT 1",
                (alias, entity_id),
            ).fetchone()
            if conflict:
                continue
            db.execute(
                "INSERT OR IGNORE INTO aliases(entity_id,alias,confidence,source) VALUES(?,?,?,?)",
                (entity_id, alias, min(confidence, 1.0), f"ai:{session_id}"),
            )
            if db.execute("SELECT changes()").fetchone()[0]:
                learned_aliases.append({"entity_id": entity_id, "alias": alias, "confidence": min(confidence, 1.0)})
    new_entities = []
    staged_entities = []
    for item in payload.get("new_entities", [])[:10]:
        entity = _stage_or_promote_entity(session_id, item)
        if entity is not None:
            new_entities.append({"id": entity["id"], "canonical_name": entity["canonical_name"], "type": entity["type"]})
        else:
            staged_entities.append({"canonical_name": str(item.get("canonical_name", ""))[:120], "type": str(item.get("type", ""))})
    from .memory_files import sync_memory_files

    sync_memory_files()
    return {"memory_learning_status": "complete", "memory_facts_learned": learned, "memory_ai_aliases_learned": learned_aliases, "memory_entities_learned": new_entities, "memory_entities_staged": staged_entities, "memory_provider": provider, "memory_model": model}
