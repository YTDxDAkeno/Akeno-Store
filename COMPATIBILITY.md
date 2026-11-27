# Kompatibilität

| Ziel | Stand |
| --- | --- |
| PS5 mit Homebrew-Umgebung, ShadowMountPlus 1.7+ | Zielplattform; **nicht getestet** ([STATUS.md](STATUS.md)) |
| Firmware | Bausteine sind upstream belegt: Boilerplate-Titel auf 6.02 und 12.70, SDL2-Software-Renderer (OpenRCT2) auf 13.00, ProsperoStore-Installationen auf 6.02/12.70. Für Akeno selbst liegt keine Firmware-Messung vor. |
| ELF-Loader auf Port 9021 | nur für Starts aus Akeno und Hauptmenü-Verknüpfungen nötig |
| Linux-Host (x86-64, SDL2, libcurl) | Entwicklung und Tests; alle Stores live geprüft |
| Windows | Build über WSL (`ps5/build-wsl.ps1`) |

Welche Spiele laufen, hängt von den jeweiligen Titeln und Runtimes ab; siehe deren Projektseiten und
[docs/RUNTIMES.md](docs/RUNTIMES.md). Akeno-eigene Firmware-Ergebnisse werden nach dem Konsolentest in
[STATUS.md](STATUS.md) ergänzt.
