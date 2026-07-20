#!/usr/bin/env python3
"""Inspect Yappl's persistent memory SQLite database from the terminal."""

from __future__ import annotations

import argparse
import sqlite3
import sys
from pathlib import Path
from typing import Iterable


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DATABASE = PROJECT_ROOT / "backend" / "data" / "memory.sqlite3"


def print_rows(rows: Iterable[sqlite3.Row]) -> None:
    values = list(rows)
    if not values:
        print("(no rows)")
        return

    columns = list(values[0].keys())
    widths = {
        column: min(
            60,
            max(len(column), *(len(str(row[column] if row[column] is not None else "")) for row in values)),
        )
        for column in columns
    }

    def formatted(row: sqlite3.Row) -> str:
        cells = []
        for column in columns:
            value = str(row[column] if row[column] is not None else "")
            if len(value) > widths[column]:
                value = value[: widths[column] - 1] + "…"
            cells.append(value.ljust(widths[column]))
        return " | ".join(cells)

    print(" | ".join(column.ljust(widths[column]) for column in columns))
    print("-+-".join("-" * widths[column] for column in columns))
    for row in values:
        print(formatted(row))


QUERIES = {
    "entities": """
        SELECT id, type, canonical_name, description, created_at, updated_at
        FROM entities ORDER BY type, canonical_name
    """,
    "aliases": """
        SELECT e.canonical_name, a.alias, a.confidence, a.source, a.pronunciation
        FROM aliases a JOIN entities e ON e.id = a.entity_id
        ORDER BY e.canonical_name, a.alias
    """,
    "facts": """
        SELECT e.canonical_name, f.predicate, f.value, f.confidence,
               f.status, f.source_session_id
        FROM facts f JOIN entities e ON e.id = f.entity_id
        ORDER BY e.canonical_name, f.updated_at DESC
    """,
    "corrections": """
        SELECT session_id, original, replacement, confidence, entity_id, created_at
        FROM transcript_corrections ORDER BY id DESC
    """,
    "pending": """
        SELECT type, canonical_name, description, mention_count,
               first_session_id, last_session_id, updated_at
        FROM pending_entities ORDER BY type, canonical_name
    """,
    "tables": """
        SELECT name, type FROM sqlite_master
        WHERE type IN ('table', 'view') AND name NOT LIKE 'sqlite_%'
        ORDER BY type, name
    """,
}


def validate_read_only_query(query: str) -> str:
    stripped = query.strip().rstrip(";").strip()
    first_word = stripped.split(maxsplit=1)[0].lower() if stripped else ""
    if first_word not in {"select", "with", "pragma", "explain"}:
        raise ValueError("custom queries must be read-only SELECT, WITH, PRAGMA, or EXPLAIN statements")
    if ";" in stripped:
        raise ValueError("only one SQL statement may be run at a time")
    return stripped


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "view",
        nargs="?",
        choices=[*QUERIES, "all", "schema", "query"],
        default="all",
        help="information to display (default: all)",
    )
    parser.add_argument("--db", type=Path, default=DEFAULT_DATABASE, help="path to memory.sqlite3")
    parser.add_argument("--sql", help="read-only SQL used with the 'query' view")
    args = parser.parse_args()

    database = args.db.expanduser().resolve()
    if not database.exists():
        print(f"Memory database not found: {database}", file=sys.stderr)
        print("Start the backend or complete a session first, or provide --db PATH.", file=sys.stderr)
        return 1

    # URI read-only mode guarantees this inspection tool cannot alter memory.
    connection = sqlite3.connect(f"file:{database}?mode=ro", uri=True)
    connection.row_factory = sqlite3.Row
    try:
        print(f"Database: {database}")
        if args.view == "schema":
            print_rows(
                connection.execute(
                    "SELECT name, sql FROM sqlite_master WHERE sql IS NOT NULL ORDER BY type, name"
                )
            )
        elif args.view == "query":
            if not args.sql:
                parser.error("query requires --sql 'SELECT ...'")
            print_rows(connection.execute(validate_read_only_query(args.sql)))
        elif args.view == "all":
            for name in ("entities", "aliases", "facts", "pending", "corrections"):
                print(f"\n{name.upper()}")
                print_rows(connection.execute(QUERIES[name]))
        else:
            print_rows(connection.execute(QUERIES[args.view]))
    except (sqlite3.Error, ValueError) as error:
        print(f"Unable to inspect database: {error}", file=sys.stderr)
        return 2
    finally:
        connection.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
