# Drittanbieter und Lizenzen

Akeno-Code: GPL-3.0-or-later ([LICENSE](LICENSE)). Der PS5-Titel linkt GPL-3.0-Komponenten (Runtime,
Startcode), deshalb wird er als Ganzes unter der GPL-3.0 verbreitet.

## Im PS5-Titel enthalten

| Komponente | Version / Commit | Verwendung | Lizenz |
| --- | --- | --- | --- |
| [ps5-native-app-boilerplate](https://github.com/drakmor/ps5-native-app-boilerplate) | `470695e` | `app_crt.cpp`, PIE-Layout, `ps5-native-tool` (Build), `sce_module/libc.prx` | GPL-3.0-or-later |
| [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk) | v0.42 | Header, Systemstubs, libc++/libc++abi/libunwind (statisch) | GPL-3.0 (Dateien mit eigenen Hinweisen; libc++ Apache-2.0 WITH LLVM-exception) |
| [lowbit/ps5-openrct2](https://github.com/lowbit/ps5-openrct2) Runtime | `e2d9a82` | `ps5/runtime/*` | GPL-3.0 |
| [ps5-payload-dev/SDL](https://github.com/ps5-payload-dev/SDL) | `ee4c47dc` + OpenRCT2-Patches | Video, Audio, Controller | zlib |
| [Monocypher](https://github.com/LoupVaillant/Monocypher) | `0d85f98` | Ed25519 | CC0 / BSD-2-Clause |
| [yyjson](https://github.com/ibireme/yyjson) | `9ddba00` | JSON | MIT |
| [miniz](https://github.com/richgel999/miniz) | `293d4db` | ZIP | MIT |
| [PicoSHA2](https://github.com/okdshin/PicoSHA2) | `161cb3f` | SHA-256 | MIT |
| [stb](https://github.com/nothings/stb) | `2c980bb` | Bilder, TrueType, PNG | MIT / Public Domain |
| [QR Code generator](https://github.com/nayuki/QR-Code-generator) | v1.8.0 | QR-Codes | MIT |
| [Inter](https://github.com/rsms/inter) | 4.x | Schrift | SIL OFL 1.1 (`native/assets/fonts/Inter-LICENSE.txt`) |
| [EasyRPG Player](https://github.com/EasyRPG/Player) | 0.8.1.1 (`78328fa`) | Eingebaute RPG-Maker-2000/2003-Runtime | GPL-3.0-or-later |
| [liblcf](https://github.com/EasyRPG/liblcf) | 0.8.1 (`92c4450`) | RPG-Maker-Datenformate | MIT |
| [pixman](https://gitlab.freedesktop.org/pixman/pixman) | 0.42.2 (Ubuntu-Quellarchiv, SHA-256 `ea1480ef…`) | Pixel-Operationen der RPG-Maker-Runtime | MIT |
| [libpng](https://github.com/pnggroup/libpng) | 1.6.48 (`ea12796`) | PNG-Grafiken der Spiele | libpng License v2 |
| [zlib](https://zlib.net) | 1.3.2 | Kompression (libpng, Player) | zlib |
| [{fmt}](https://github.com/fmtlib/fmt) | 10.2.1 | Textformatierung (nur Header) | MIT |
| [inih](https://github.com/benhoyt/inih) | r58 | INI-Dateien der Spiele | BSD-3-Clause |

Die Bibliotheksdateien sind mit den SHA-256-Werten aus ProsperoStores `third_party/STORE_SOURCES.json`
geprüft übernommen. Designvorlagen ohne Codeübernahme: MihawkOS/PS5_RetroArch-Forwarder
(Start-Payload, Kacheln), ProsperoStore (`sceHttp`-Transport, Titelordner-Regeln, `_fstatfs`).

## Host-Build

SDL2 und libcurl der Distribution; der Python-Host-Hub nutzt nur die Standardbibliothek.

## Inhalte

Signal Drift (Host-Hub) steht unter MIT ([licenses/SIGNAL-DRIFT.txt](licenses/SIGNAL-DRIFT.txt)).
Katalogtexte und Artwork gehören den jeweiligen Entwicklern und werden zur Laufzeit von den Stores
geladen. Der Test-Fixture-Ordner `native/tests/fixtures/prosperostore` enthält einen unveränderten,
signierten Katalogstand (siehe dortige NOTICE).
