"""Reviewed public release sources and scope notes for catalog discovery."""

from dataclasses import dataclass


@dataclass(frozen=True)
class GitHubProject:
    repository: str
    title: str
    genre: str
    description: str
    license: str
    target_platform: str
    age_rating: str = "unrated"


GITHUB_PC_PROJECTS = (
    GitHubProject(
        "Anuken/Mindustry", "Mindustry", "Strategie",
        "Automatisierung, Fabrikbau und Verteidigung in einem Open-Source-Spiel. "
        "Der GitHub-Feed veröffentlicht die Upstream-Versionen.",
        "GPL-3.0 (Spielcode)", "pc",
    ),
    GitHubProject(
        "EasyRPG/Player", "EasyRPG Player", "Runtime",
        "Open-Source-Player für Projekte aus RPG Maker 2000 und 2003. "
        "Benötigt ein separat bezogenes Spiel.",
        "GPL-3.0 (Player)", "pc",
    ),
    GitHubProject(
        "supertuxkart/stk-code", "SuperTuxKart", "Rennspiel",
        "Open-Source-Kart-Rennspiel. Der Feed gehört zum Originalprojekt und "
        "enthält keine von Akeno geprüften PS5-Pakete.",
        "GPL (Spielcode)", "pc",
    ),
    GitHubProject(
        "open-goal/jak-project", "OpenGOAL", "Action-Adventure",
        "PC-Port für Jak and Daxter mit separat benötigten eigenen Spieldaten. "
        "Die von Upstream unterstützte Ausgabe muss zum eigenen Datenträger passen.",
        "ISC (Projektcode)", "pc",
    ),
    GitHubProject(
        "HarbourMasters/Shipwright", "Ship of Harkinian", "Adventure",
        "Projekt für Ocarina of Time auf den von Upstream angegebenen "
        "Plattformen. Spielinhalte stammen aus einer separat benötigten, "
        "rechtmäßig erworbenen Fassung.",
        "Getrennte Bedingungen für Code und Assets", "pc",
    ),
)

GITHUB_PS5_PROJECTS = (
    GitHubProject(
        "mateo-cogeanu/ProsperoCraft", "ProsperoCraft", "Launcher",
        "Upstream-Projekt für Minecraft Java auf PS5. Spiel und Kontolizenz "
        "werden separat benötigt. Konsolenstart in Akeno ist ungeprüft.",
        "GPL-3.0-or-later (Launchercode)", "ps5",
    ),
    GitHubProject(
        "Shabbypenguin/PokeMMO-Prospero", "PokeMMO Prospero", "Online-Rollenspiel",
        "Unabhängiger PS5-Port. Benötigt separate Nutzerdaten und die "
        "Autorisierung des Spielanbieters. Akeno hat den Port nicht gestartet.",
        "GPL-3.0-or-later (Portcode)", "ps5",
    ),
    GitHubProject(
        "griches/PS5_Dusklight", "Dusklight für PS5", "Adventure",
        "PS5-Port für Twilight Princess. Benötigt eine eigene, von Upstream "
        "unterstützte Spielfassung. Konsolenstart in Akeno ist ungeprüft.",
        "GPL-3.0 (Portcode)", "ps5",
    ),
)
