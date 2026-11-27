"""Discovery adapter for selected public ProsperoStore game records."""

import re

from .projects import GitHubProject
from .providers import CatalogError, _fetch, _json, _text

CATALOG_RECORDS = "https://raw.githubusercontent.com/darkxex/ps5-homebrew-catalog/main/apps"
PROJECTS = (
    (
        "PPSA28319",
        GitHubProject(
            "STL1te/MedievalTennis", "Middle Ages Ping Pong", "Arcade",
            "Tischtennis in einem mittelalterlichen Szenario für einen oder zwei lokale Spieler.",
            "MIT (Projektcode)", "ps5",
        ),
    ),
    (
        "PPSA69421",
        GitHubProject(
            "Marice/stk-ps5", "SuperTuxKart für PS5", "Rennspiel",
            "Alpha-Port von SuperTuxKart 1.5 mit lokalen Rennen. "
            "Netzwerkspiel ist laut Quellkatalog deaktiviert.",
            "GPL-3.0-or-later (Portcode)", "ps5",
        ),
    ),
    (
        "PPSA99320",
        GitHubProject(
            "damiantw/PS5_Shipwright", "Ship of Harkinian für PS5", "Adventure",
            "Vulkan-Port für Ocarina of Time. Benötigt eine separat erworbene, "
            "von Upstream unterstützte Spielfassung.",
            "GPL-3.0 (Portcode)", "ps5",
        ),
    ),
    ("PPSA50001", GitHubProject(
        "Bruce303lee/sudokuzen", "SudokuZen", "Logik",
        "Sudoku mit sechs Designs und einer Demo der Lösungsverfahren.",
        "GPL-3.0 (Portcode)", "ps5", "all",
    )),
    ("PPSA58001", GitHubProject(
        "RedHotBallBag/lantern-miner-ps5", "Lantern Miner", "Plattformspiel",
        "Plattformspiel mit Höhlen und Laternen. Der Katalog führt eine Beta.",
        "GPL-3.0 (Portcode)", "ps5", "all",
    )),
    ("PPSA92041", GitHubProject(
        "plutobox/ps5-crawl", "Dungeon Crawl Stone Soup", "Roguelike",
        "Roguelike mit Kachelgrafik. Der Katalog nennt Controller-Unterstützung.",
        "GPL-2.0-or-later (Portcode)", "ps5", "teen",
    )),
    ("PPSA92042", GitHubProject(
        "plutobox/ps5-NetHack", "NetHack", "Roguelike",
        "Konsolen-Port von NetHack 5.0 mit Controller-Menüs.",
        "NGPL (Portcode)", "ps5", "teen",
    )),
    ("PPSA92043", GitHubProject(
        "plutobox/ps5-BrogueCE", "Brogue CE", "Roguelike",
        "Rundenbasiertes Einzelspiel in zufällig erzeugten Dungeons.",
        "AGPL-3.0 (Portcode)", "ps5", "teen",
    )),
    ("PPSA99006", GitHubProject(
        "BlackBearRevived/ProsperoPuzzles", "ProsperoPuzzles", "Logik",
        "Sammlung mit 50 Logikrätseln.",
        "GPL-3.0 (Portcode)", "ps5", "all",
    )),
    ("PPSA99728", GitHubProject(
        "lowbit/ps5-openrct2", "OpenRCT2", "Simulation",
        "Port der freien Engine. Benötigt rechtmäßig erworbene RollerCoaster-Tycoon-2-Spielinhalte.",
        "GPL-3.0 (Portcode)", "ps5", "all",
    )),
)


def _cover(record, project):
    if project.age_rating not in {"all", "teen"}:
        return {}
    icon = record.get("icon_url")
    pattern = rf"https://raw\.githubusercontent\.com/{re.escape(project.repository)}/[a-z0-9._-]+/sce_sys/icon0\.png"
    if isinstance(icon, str) and re.fullmatch(pattern, icon, re.IGNORECASE):
        return {"cover_url": icon}
    return {}


class ProsperoStoreProvider:
    """Read reviewed IDs from the store's published source records."""

    def list_games(self) -> list[dict]:
        games = []
        for title_id, project in PROJECTS:
            metadata_url = f"{CATALOG_RECORDS}/{title_id}.json"
            record = _json(_fetch(metadata_url, "application/json"))
            source = f"https://github.com/{project.repository}"
            if (
                not isinstance(record, dict)
                or record.get("titleid") != title_id
                or record.get("kind") != "game"
                or not isinstance(record.get("source_repo"), str)
                or record["source_repo"].casefold() != source.casefold()
                or record.get("reserved") is True
                or record.get("withdrawn") is True
            ):
                raise CatalogError(f"ProsperoStore record {title_id} changed identity")
            version = _text(record.get("version"), "version", 160)
            games.append({
                "id": f"prosperostore-{title_id.lower()}",
                "title": project.title,
                "version": version,
                "source_url": f"{source}/releases",
                "metadata_url": metadata_url,
                "provider": "prosperostore",
                "repository": project.repository,
                "developer": project.repository.split("/")[0],
                "age_rating": project.age_rating,
                "description": project.description,
                "genre": project.genre,
                "license": project.license,
                "target_platform": project.target_platform,
                **_cover(record, project),
            })
        return games
