"""Heuristic engine hints and evidence-based gameplay status."""

from pathlib import PurePosixPath


def detect_engine(paths) -> str | None:
    """Provide name-based hints when only a file listing is available."""
    names = {PurePosixPath(path).name.lower() for path in paths}
    if "rmmz_core.js" in names:
        return "RPG Maker MZ"
    if "rpg_core.js" in names:
        return "RPG Maker MV"
    suffixes = {PurePosixPath(name).suffix for name in names}
    for extension, engine in (
        (".pck", "Godot"),
        (".rpa", "Ren'Py"),
        (".jar", "Java"),
        (".exe", "Windows"),
        (".html", "HTML5"),
        (".htm", "HTML5"),
    ):
        if extension in suffixes:
            return engine
    return None


def catalog_status(game: dict) -> str:
    if game.get("archive_url"):
        return "Noch nicht getestet"
    return "Katalogeintrag"
