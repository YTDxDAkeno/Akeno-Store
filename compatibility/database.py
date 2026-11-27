"""Local observations, keyed by game and version."""

import sqlite3
from contextlib import closing
from datetime import UTC, datetime
from pathlib import Path


class CompatibilityDB:
    def __init__(self, path: Path):
        self.path = path
        with closing(sqlite3.connect(path)) as connection, connection:
            connection.execute("""
                CREATE TABLE IF NOT EXISTS observations (
                    id TEXT NOT NULL, version TEXT NOT NULL, title TEXT NOT NULL,
                    provider TEXT, runtime TEXT NOT NULL, runtime_version TEXT NOT NULL,
                    engine TEXT, installed INTEGER NOT NULL,
                    player_served INTEGER NOT NULL DEFAULT 0, observed_at TEXT NOT NULL,
                    PRIMARY KEY (id, version, runtime, runtime_version)
                )
            """)

    def observe(self, game: dict, runtime_version: str, *, engine=None, served=False):
        with closing(sqlite3.connect(self.path)) as connection, connection:
            connection.execute("""
                INSERT INTO observations VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT (id, version, runtime, runtime_version) DO UPDATE SET
                    installed = excluded.installed,
                    engine = COALESCE(excluded.engine, engine),
                    player_served = MAX(player_served, excluded.player_served),
                    observed_at = excluded.observed_at
            """, (
                game["id"], game["version"], game["title"], game.get("provider"),
                game["runtime"], runtime_version, engine, 1, int(served),
                datetime.now(UTC).isoformat(),
            ))

    def list_records(self) -> list[dict]:
        with closing(sqlite3.connect(self.path)) as connection:
            connection.row_factory = sqlite3.Row
            rows = connection.execute("SELECT * FROM observations ORDER BY observed_at DESC")
            return [dict(row) | {"platform": "host", "gameplay": "unconfirmed"} for row in rows]
