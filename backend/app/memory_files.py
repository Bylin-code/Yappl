import re
from pathlib import Path

from .settings import settings


CATEGORY_FOLDERS = {
    "person": "people",
    "place": "places",
    "object": "objects",
    "event": "events",
    "project": "projects",
    "organization": "organizations",
}


def memory_directory() -> Path:
    return Path(settings.yappl_storage_dir) / "memory"


def _slug(value: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")
    return slug or "unnamed"


def _write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(content.rstrip() + "\n")
    temporary.replace(path)


def _entity_markdown(entity: dict) -> str:
    lines = [
        f"# {entity['canonical_name']}",
        "",
        f"- Type: {entity['type']}",
        f"- Description: {entity['description'] or 'No description yet.'}",
        f"- Memory ID: `{entity['id']}`",
        "",
        "## Known names",
        "",
    ]
    aliases = entity.get("aliases", [])
    lines.extend(
        f"- {alias['alias']} — confidence {float(alias['confidence']):.2f}, source `{alias['source']}`"
        for alias in aliases
    )
    if not aliases:
        lines.append("- None")
    lines.extend(["", "## Current facts", ""])
    facts = entity.get("facts", [])
    lines.extend(
        f"- **{fact['predicate'].replace('_', ' ').title()}**: {fact['value']}"
        + (f" _(source: `{fact['source_session_id']}`)_" if fact.get("source_session_id") else "")
        for fact in facts
    )
    if not facts:
        lines.append("- None yet")
    return "\n".join(lines)


def sync_memory_files() -> Path:
    """Regenerate human-readable Markdown mirrors from authoritative SQLite memory."""
    # Import lazily to avoid a module cycle during memory initialization.
    from .memory import list_entities, list_pending_entities

    root = memory_directory()
    entities = list_entities()
    expected: set[Path] = set()
    for folder in CATEGORY_FOLDERS.values():
        (root / folder).mkdir(parents=True, exist_ok=True)

    for entity in entities:
        folder = CATEGORY_FOLDERS.get(entity["type"], "other")
        path = root / folder / f"{_slug(entity['canonical_name'])}.md"
        _write(path, _entity_markdown(entity))
        expected.add(path)

    pending = list_pending_entities()
    pending_lines = [
        "# Pending Recurring Memories",
        "",
        "These candidates have been identified once and will be promoted after appearing in another session.",
        "",
    ]
    pending_lines.extend(
        f"- **{item['canonical_name']}** ({item['type']}): {item['description']} — mentions: {item['mention_count']}"
        for item in pending
    )
    if not pending:
        pending_lines.append("- None")
    pending_path = root / "pending.md"
    _write(pending_path, "\n".join(pending_lines))
    expected.add(pending_path)

    readme = root / "README.md"
    _write(
        readme,
        """# Yappl Memory

This directory is a readable mirror of `memory.sqlite3`. SQLite remains the source of truth; these Markdown files are regenerated automatically after memory changes.

- `people/`: recurring people and relationship facts
- `places/`: personally important recurring places
- `objects/`: important possessions such as a car
- `events/`: significant recurring or anticipated events
- `pending.md`: first-mention candidates awaiting recurrence
""",
    )
    expected.add(readme)

    return root
