"""Catalog snapshots and background installation jobs for the host frontend."""

from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict
from pathlib import Path
from threading import RLock

from compatibility.database import CompatibilityDB
from compatibility.inspection import RuntimeCapability, inspect_path, resolve_runtime
from compatibility.profiles import catalog_status
from core.bundled import bundle
from runtimes.html5 import RUNTIME, supports
from security.installer import Installer


class Hub:
    def __init__(self, data: Path, providers: list[tuple[str, object]]):
        self.providers = providers
        self.installer = Installer(data / "library")
        self.bundled, self.bundled_archive = bundle(data / "bundled")
        self.observations = CompatibilityDB(data / "compatibility.sqlite3")
        self.lock = RLock()
        self.workers = ThreadPoolExecutor(max_workers=3, thread_name_prefix="akeno")
        self.sources = {"Akeno": [self.bundled]}
        self.states = {"Akeno": "ready", **{name: "pending" for name, _ in providers}}
        self.errors: dict[str, str] = {}
        self.jobs: dict[str, dict] = {}
        self.syncing = False

    def close(self):
        self.workers.shutdown(wait=True)

    def catalog(self) -> dict:
        with self.lock:
            games = {}
            for entries in self.sources.values():
                for game in entries:
                    game_id = game["id"]
                    if game_id not in games:
                        games[game_id] = self._catalog_entry(game)
            return {
                "games": list(games.values()),
                "sources": [
                    {"name": name, "state": state, "count": len(self.sources.get(name, []))}
                    for name, state in self.states.items()
                ],
                "errors": [{"source": name, "message": error} for name, error in self.errors.items()],
            }

    @staticmethod
    def _catalog_entry(game: dict) -> dict:
        result = dict(game)
        result["installable"] = supports(game) and all(
            game.get(field) for field in ("archive_url", "sha256", "license", "version")
        )
        result["compatibility"] = catalog_status(game)
        return result

    def sync(self) -> dict:
        with self.lock:
            if self.syncing:
                return {"state": "syncing"}
            if not self.providers:
                return {"state": "ready"}
            self.syncing = True
            for name, _ in self.providers:
                self.states[name] = "syncing"
            self.workers.submit(self._refresh_sources)
            return {"state": "syncing"}

    def _refresh_sources(self):
        try:
            for name, provider in self.providers:
                try:
                    entries = provider.list_games()
                except Exception as exc:
                    with self.lock:
                        self.states[name] = "error"
                        self.errors[name] = str(exc)
                else:
                    with self.lock:
                        self.states[name] = "ready"
                        self.errors.pop(name, None)
                        self.sources[name] = entries
        finally:
            with self.lock:
                self.syncing = False

    def library(self) -> dict:
        return {"games": self.installer.list_installed()}

    def compatibility(self) -> dict:
        return {"records": self.observations.list_records()}

    def status(self) -> dict:
        with self.lock:
            return {
                "platform": "Host · HTML5",
                "runtimes": [RUNTIME],
                "jobs": [dict(job) for job in self.jobs.values()],
            }

    def install(self, game_id: str) -> dict:
        game = next((item for item in self.catalog()["games"] if item["id"] == game_id), None)
        if game is None:
            raise KeyError(game_id)
        if not game["installable"]:
            raise ValueError("Für dieses Spiel ist kein geprüftes HTML5-Paket verfügbar.")
        return self._job(game_id, "install", game)

    def uninstall(self, game_id: str) -> dict:
        if not any(game["id"] == game_id for game in self.library()["games"]):
            raise KeyError(game_id)
        return self._job(game_id, "uninstall")

    def _job(self, game_id: str, action: str, game: dict | None = None) -> dict:
        with self.lock:
            current = self.jobs.get(game_id)
            if current and current["state"] in {"queued", "running"}:
                raise ValueError("Für dieses Spiel läuft bereits ein Auftrag.")
            job = {
                "id": game_id,
                "action": action,
                "state": "queued",
                "downloaded_bytes": 0,
                "total_bytes": None,
            }
            self.jobs[game_id] = job
            self.workers.submit(self._run_job, job, game)
            return dict(job)

    def _run_job(self, job: dict, game: dict | None):
        def progress(downloaded, total):
            with self.lock:
                job.update(downloaded_bytes=downloaded, total_bytes=total)

        with self.lock:
            job["state"] = "running"
        try:
            if game is not None:
                options = {"local_archive": self.bundled_archive} if game["id"] == self.bundled["id"] else {}
                self.installer.install(game, progress, **options)
                file = self.installer.entrypoint(game["id"])
                root = file.parents[len(Path(game["entrypoint"]).parts) - 1]
                inspection = inspect_path(root, entrypoint=game["entrypoint"])
                finding = inspection.primary
                job_engine = finding.engine if finding else None
                capabilities = (RuntimeCapability(RUNTIME["id"], "host", ("HTML5",), version=int(RUNTIME["version"])),)
                resolution = resolve_runtime(finding, capabilities, platform="host")
                self.observations.observe(game, RUNTIME["version"], engine=job_engine)
                with self.lock:
                    job["engine"] = job_engine
                    job["runtime_resolution"] = asdict(resolution)
            else:
                self.installer.uninstall(job["id"])
        except Exception as exc:
            with self.lock:
                job.update(state="failed", error=str(exc))
        else:
            with self.lock:
                job["state"] = "completed"
