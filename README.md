# Akeno Gaming Hub – PS5 Edition

Akeno bündelt Spielekatalog, Bibliothek, Paketinstallation und Laufzeiten. Für das PS5-Ziel entsteht eine native SDL2-Oberfläche. Der Linux-Entwicklungsbuild exportiert Katalog und Bibliothek für diese Oberfläche und startet lokale HTML5-Spiele in einer isolierten Browseransicht. Signal Drift ist ein mitgeliefertes, spielbares Puzzle für einen vollständigen Offline-Durchlauf. Der PS5-Zielbuild erzeugt ein gelinktes SDL2-Modul. Ein installierbares Konsolenpaket steht aus.

Der [native Build](BUILD.md#native-sdl-oberfläche) verwendet Katalogdaten und Bibliothek des Entwicklungs-Hubs. [PS5-Ziel und Konsolentests](platform/ps5/README.md)

## Schnellstart

```sh
python3 -m core.server --offline --data-dir /tmp/akeno-hub
```

Öffne im Browser die beim Start ausgegebene lokale Adresse. Wähle **Signal Drift → Installieren → Spielen**. Falls du GitHub- und itch.io-Metadaten abrufen möchtest, starte ohne `--offline` und aktualisiere den Katalog. Eigene geprüfte Kataloge kannst du über `--manifest DATEI` einbinden.

`make test` führt die Host-Tests aus. `make build` prüft den Build und erstellt ein portables Host-Quellpaket. Die Kommandos benötigen Python 3.12+, einen aktuellen Browser und GNU Make für die Make-Ziele.

- [Build](BUILD.md) und [Installation](INSTALLATION.md)
- [Architektur](ARCHITECTURE.md) und [Anbieter- und Runtime-Schnittstellen](docs/SDK.md)
- [Recherche](RESEARCH.md) und [Kompatibilität](COMPATIBILITY.md)
- [Sicherheit](SECURITY.md), [bekannte Einschränkungen](KNOWN_ISSUES.md) und [Entwicklungsstand](STATUS.md)
