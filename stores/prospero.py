"""prosperostore.net signed catalog (API v1, schema 3) for the host hub.

Contract: https://github.com/darkxex/ps5-homebrew-catalog/blob/main/docs/api.md
manifest.json is Ed25519-signed (manifest.sig, 64 raw bytes) by either published key,
its sequence never decreases, and every API file used must match the SHA-256 it lists.
The native app (native/src/stores/prosperostore.cpp) implements the same checks.
"""

import hashlib
from pathlib import Path

from security.ed25519 import verify

from .providers import CatalogError, _fetch, _json, _text

API = "https://prosperostore.net/api/v1/"
KEYS = (
    bytes.fromhex("87391bf1698ecef101bf5e29dc8585ee5947d571e19470de7411c5d3b137b5cf"),  # da351006acb6e3c3
    bytes.fromhex("509bcfab7edfb4e5ed23639488517c6ef2657c13b6b7f2bf699c8989d9b0dd7b"),  # eef399ea3007720a
)
CATEGORIES = {
    "console-ports": "Konsolen-Port", "pc-ports": "PC-Port", "emulators": "Emulator",
    "utilities": "Werkzeug", "tools": "Werkzeug", "video": "Video", "homebrew-games": "Homebrew",
    "streaming": "Streaming", "music": "Musik", "ai": "KI", "showcases": "Showcase",
}


def verified_index(manifest: bytes, signature: bytes, index: bytes, highest_sequence: int = -1) -> tuple[list, int]:
    """Checks signature, rollback and index hash; returns (apps, sequence)."""
    if not any(verify(key, manifest, signature) for key in KEYS):
        raise CatalogError("ProsperoStore catalog signature does not verify")
    document = _json(manifest)
    if not isinstance(document, dict) or document.get("schema") != 3 or not isinstance(document.get("files"), dict):
        raise CatalogError("ProsperoStore manifest has an unexpected shape")
    sequence = document.get("sequence")
    if type(sequence) is not int or sequence < highest_sequence:
        raise CatalogError("ProsperoStore catalog is older than one already accepted")
    if hashlib.sha256(index).hexdigest() != str(document["files"].get("index.json", "")).lower():
        raise CatalogError("ProsperoStore index.json does not match the signed manifest")
    listing = _json(index)
    if not isinstance(listing, dict) or listing.get("schema") != 3 or not isinstance(listing.get("apps"), list):
        raise CatalogError("ProsperoStore index.json has an unexpected shape")
    return listing["apps"] + list(listing.get("shared") or []), sequence


def _entry(app: dict) -> dict | None:
    title_id = app.get("titleid")
    if not isinstance(title_id, str) or len(title_id) != 9 or app.get("status") not in {"available", "coming_soon"}:
        return None
    app_id = app.get("id") if isinstance(app.get("id"), str) else title_id
    game = {
        "id": f"prosperostore-{app_id.lower()}",
        "title": _text(app.get("name"), "name", 160),
        "version": app.get("version") or "",
        "content_version": app.get("content_version") or "",
        "title_id": title_id,
        "provider": "prosperostore",
        "developer": app.get("author") or app.get("owner") or "",
        "description": app.get("description") or "",
        "genre": CATEGORIES.get(app.get("category"), "Homebrew"),
        "target_platform": "ps5",
        "age_rating": "unrated",
        "format": app.get("format"),
        "status": app.get("status"),
        "source_url": f"https://prosperostore.net/app/{app_id}/",
        "signed_catalog": True,
    }
    icon = app.get("icon_small") or app.get("icon")
    if isinstance(icon, str) and icon.startswith("https://prosperostore.net/api/v1/icons/"):
        game["cover_url"] = icon
    return game


class ProsperoStoreProvider:
    """Every listed app from the verified catalog (no hand-picked subset)."""

    def __init__(self, state_dir: Path | None = None):
        self.state = Path(state_dir) / "prosperostore.sequence" if state_dir else None

    def _highest(self) -> int:
        try:
            return int(self.state.read_text()) if self.state else -1
        except (OSError, ValueError):
            return -1

    def list_games(self) -> list[dict]:
        manifest = _fetch(API + "manifest.json", "application/json")
        signature = _fetch(API + "manifest.sig", "application/octet-stream")
        index = _fetch(API + "index.json", "application/json")
        apps, sequence = verified_index(manifest, signature, index, self._highest())
        if self.state:
            self.state.parent.mkdir(parents=True, exist_ok=True)
            self.state.write_text(str(sequence))
        games = []
        for app in apps:
            if isinstance(app, dict) and (game := _entry(app)):
                games.append(game)
        if not games:
            raise CatalogError("ProsperoStore catalog lists no apps")
        return games

