#!/usr/bin/env python3
"""Regenerate readable Yappl memory files from memory.sqlite3."""

import subprocess
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT / "backend"))

if __name__ == "__main__":
    try:
        from app.settings import settings

        if settings.yappl_storage_dir == "/data":
            settings.yappl_storage_dir = str(PROJECT_ROOT / "backend" / "data")
        from app.memory_files import sync_memory_files

        print(sync_memory_files())
    except ModuleNotFoundError:
        # The host may not have backend Python dependencies installed. Use the
        # already configured backend container in that case.
        result = subprocess.run(
            [
                "docker",
                "compose",
                "exec",
                "-T",
                "api",
                "python",
                "-c",
                "from app.memory_files import sync_memory_files; print(sync_memory_files())",
            ],
            cwd=PROJECT_ROOT / "backend",
            check=False,
        )
        raise SystemExit(result.returncode)
