"""
planc database migration runner.

Applies all pending *.sql files from the migrations/ directory in
lexicographic order.  Applied migrations are recorded in a _migrations
table inside planc.db so each script runs exactly once.

Usage:
    python3 api/migrate.py            # from project root
    python3 /app/api/migrate.py       # inside the Docker container
"""

import os
import sqlite3
import sys
from pathlib import Path

DB_PATH        = os.environ.get("PLANC_DB_PATH",
                    os.path.expanduser("~/.local/share/plan/planc.db"))
MIGRATIONS_DIR = Path(__file__).parent.parent / "migrations"


def run() -> None:
    Path(DB_PATH).parent.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect(DB_PATH)
    db.execute("PRAGMA journal_mode=WAL")

    db.execute("""
        CREATE TABLE IF NOT EXISTS _migrations (
            name       TEXT PRIMARY KEY,
            applied_at TEXT NOT NULL DEFAULT (datetime('now'))
        )
    """)
    db.commit()

    applied = {row[0] for row in db.execute("SELECT name FROM _migrations")}
    pending = sorted(p for p in MIGRATIONS_DIR.glob("*.sql")
                     if p.name not in applied)

    if not pending:
        print("[migrate] already up to date")
        db.close()
        return

    for path in pending:
        sql = path.read_text()
        print(f"[migrate] applying {path.name} ...", end=" ", flush=True)
        try:
            db.executescript(sql)
        except sqlite3.OperationalError as e:
            # "duplicate column name" means the column was added by the old
            # storage_db_open fallback — treat it as already applied.
            if "duplicate column name" in str(e).lower():
                print("(column already present, skipping)")
            else:
                print(f"FAILED\n[migrate] error: {e}")
                db.close()
                sys.exit(1)
        else:
            print("ok")
        db.execute("INSERT INTO _migrations (name) VALUES (?)", (path.name,))
        db.commit()

    db.close()
    print("[migrate] done")


if __name__ == "__main__":
    run()
