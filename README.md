# Akeno Gaming Hub – PS5 Edition

**One Hub. Multiple Stores. Universal Gaming.**

Akeno ist ein nativer Homebrew-Hub für die PS5: ein Katalog aus mehreren Stores, Downloads mit
Prüfsummen, automatische Installation und Updates, eine Bibliothek, Hauptmenü-Verknüpfungen und
gemeinsame Runtimes, die vorhandene PS5-Projekte (Emulatoren, Wine, Browser) nutzen, statt jedes Spiel
einzeln zu portieren.

![Entdecken](docs/media/discover.jpg)

| Store | Was Akeno damit macht |
| --- | --- |
| [ProsperoStore](https://prosperostore.net) | Vollständiger, **Ed25519-signierter** Katalog (243 Apps): installieren, aktualisieren, entfernen |
| [RecompStore](https://www.recompstore.com) | Recompilations und Ports, installiert über GitHub-Releases mit veröffentlichtem SHA-256; „bring your own disc" wird angezeigt |
| Akeno-Katalog | 38 kuratierte kostenlose, Online- und Fanspiele (PokeMMO, Pokémon Showdown, PokéRogue, TETR.IO, Luanti, OpenRA, SRB2 …) mit dem echten Weg auf die PS5 und Kompatibilitätsstatus |
| itch.io, Game Jolt | Kostenlose Indie-Spiele; Browserspiele starten direkt in Parallax Browser (oder ProsperoWeb) |
| GOG, Epic Games | Kostenlose PC-Spiele und Gratis-Aktionen zum Entdecken; Kauf/Download über das eigene Konto |
| GitHub | Eigene Repositories mit PS5-Titel-ZIPs |

Status nach Stufen: [STATUS.md](STATUS.md). Kurz: Akeno **startet auf der PS5** und lädt die Kataloge
(Nutzertest); Installationen, Starts und die neuen MihawkOS-/Browser-Wege sind auf der Konsole noch
unbestätigt. Was aus dem Master-Prompt umgesetzt ist und was fehlt:
[docs/MASTER_PROMPT_COVERAGE.md](docs/MASTER_PROMPT_COVERAGE.md).

Neu in `01.003.000`: Windows-Spiele direkt in MihawkOS installieren, Browserspiele per Startadresse,
eigene Kompatibilitätsberichte und ein Diagnose-Tab.

## Schnellstart

PS5-Paket bauen (Linux/WSL, siehe [BUILD.md](BUILD.md)):

```sh
bash ps5/build.sh
```

`ps5/dist/PPSA90410.zip` installieren und testen: [INSTALLATION.md](INSTALLATION.md).

Auf dem Entwicklungsrechner dieselbe App starten:

```sh
make native-run
```

## Aufbau

| Pfad | Inhalt |
| --- | --- |
| `native/src/core` | JSON, SHA-256/Ed25519, Dateien, ZIP-Prüfung, Bilder |
| `native/src/stores` | Provider und Zusammenführung der Kataloge |
| `native/src/install` | Download-Manager, Installer, Bibliothek |
| `native/src/runtime` | Formaterkennung, Runtime-Routing, Starts und Verknüpfungen |
| `native/src/ui` | Oberfläche (SDL2) |
| `native/src/platform` | Host (Linux, libcurl) und PS5 (`sceHttp`, Pfade, Loader) |
| `ps5/` | PS5-Build: Toolchain, Titel-Runtime, SDL-Patches, Start-Payload, Kachel, `sce_sys` |
| `core/`, `stores/`, `apps/ps5-hub/web` | Python-Host-Hub (Web-UI, HTML5-Player) |

Weitere Dokumente: [Architektur](ARCHITECTURE.md) · [Stores](docs/STORES.md) ·
[Runtimes](docs/RUNTIMES.md) · [Recherche](RESEARCH.md) · [Audit](docs/AUDIT.md) ·
[Sicherheit](SECURITY.md) · [Bekannte Einschränkungen](KNOWN_ISSUES.md) · [Roadmap](ROADMAP.md)

## Rechtliches

Akeno-Code steht unter GPL-3.0-or-later ([LICENSE](LICENSE)); Drittkomponenten behalten ihre Lizenzen
([THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)). Keine Sony-SDK-Dateien, Firmware, Schlüssel oder
geschützten Spieldaten. Recomps und Emulatoren benötigen eigene, rechtmäßig erstellte Dumps. Akeno
umgeht keine Kaufprüfungen oder Server-Autorisierungen. PlayStation und PS5 sind Marken von Sony
Interactive Entertainment; Akeno ist ein unabhängiges Projekt.
