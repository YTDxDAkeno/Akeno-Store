# Akeno-Runtimes

Spiele laufen **in Akeno selbst**. Akeno öffnet für ein Spiel keine andere App, keinen Browser und
keinen fremden Launcher. Die Quellcode-Recherche, auf der das beruht, steht in
[RUNTIME_RESEARCH.md](RUNTIME_RESEARCH.md).

## Spielmodus (`native/src/play/`)

1. „Spielen" in Akeno: Akeno legt eine offene Kompatibilitäts-Nachfrage an und startet seinen eigenen
   Titel mit `--play=<id> --from-hub` neu (`sceSystemServiceLoadExec("/app0/eboot.bin", …)`, auf dem
   Host `execv`).
2. `main()` erkennt den Spielmodus, lädt `/data/akeno/library/<id>/profile.json` und übergibt an die
   eingebaute Runtime. Diese öffnet ihr eigenes SDL-Fenster im Vollbild und übernimmt DualSense und Ton.
3. Endet das Spiel, startet Akeno mit `--return=<id>` zurück in den Hub und fragt nach dem
   Kompatibilitätsbericht. Kommt der Start von einer Hauptmenü-Kachel (`--play=<id>` ohne
   `--from-hub`), geht es danach zurück zum PS5-Hauptmenü.

Startprofil (`profile.json`): `id`, `title`, `engine`, `runtime`, `folder` (Spieldateien), `saves`,
`source` (Katalog-ID oder `import`), `version`, `cover`, `sha256`, `owned_files`, `installed`,
`last_played`.

## Runtimes

| Runtime | Stand | Grundlage |
| --- | --- | --- |
| Native PS5-Titel | eingebaut | signierter ProsperoStore, RecompStore |
| RPG Maker 2000/2003 | **eingebaut** (Host getestet, PS5 offen) | EasyRPG Player 0.8.1.1, liblcf 0.8.1 |
| Windows x86/x64 | geplant | Wine im Titel nach Prospero Win (LGPL-2.1) |
| Linux x86-64 (ELF) | geplant | ELF-Loader + Linux-ABI nach PokeMMO Prospero (GPL-3.0) |
| Java / JVM / LWJGL | geplant | OpenJDK-25-Port (ps5-java25-homebrew) |
| HTML5 / JS / WASM | geplant | WebKit (WPE) ohne Browser-Oberfläche |
| Godot | geplant | Godot-Engine mit OpenGL ES (ps5-opengl) |
| Ren'Py / Python | geplant | CPython, pygame_sdl2, Ren'Py (OpenGL ES) |
| .NET / Mono | geplant | Mono; Windows-.NET-Spiele über die Windows-Runtime |
| ROMs / Disc-Images | geplant | libretro-Kerne im Spielmodus |

## Erkennung (`native/src/runtime/runtimes.cpp`)

Gelesen werden nur Dateiköpfe und Ordnerstrukturen, nichts wird ausgeführt: `RPG_RT.ldb` (auch bis zu
drei Ebenen tief), RPG Maker MV/MZ (`js/rpg_core.js`, `js/rmmz_core.js`), Ren'Py (`game/*.rpa`),
HTML5 (`index.html`, Godot/Unity/Ren'Py-Web erkannt), Godot (`GDPC`), PE (x86/x86-64, Unity an
`<Name>_Data`, GameMaker an `data.win`), ELF, JAR/MIDlet, SWF, ROM- und Disc-Signaturen.
Jede Erkennung nennt die Engine, die zuständige Akeno-Runtime und ob diese eingebaut ist.

## Installation

Für Spiele, die eine eingebaute Runtime spielt, lädt Akeno die **originalen Spieldateien** (z. B. über
die itch.io-API), entpackt sie in `/data/akeno/library/<id>/game` und erkennt die Engine an den Dateien.
Danach legt Akeno das Startprofil an. Passt die Engine zu keiner eingebauten Runtime, installiert Akeno
nichts und nennt den Grund. Programme aus dem Download werden nie ausgeführt; die RPG-Maker-Runtime
liest nur die Spieldaten.

Eigene Spiele: Ordner nach `/data/akeno/games` (oder `/mnt/usbN/akeno/games`) kopieren. Die Bibliothek
erkennt sie, „In Akeno spielen" legt ein Profil an, ohne die Dateien zu verschieben.

RPG-Maker-Spiele, die das RTP brauchen: das RTP als eigene Kopie nach `/data/akeno/rtp/2000` bzw.
`/2003` legen.

## Hauptmenü-Kacheln

Jedes Spiel in Akenos Bibliothek kann eine eigene Kachel bekommen (`PPSA90500`–`PPSA90599`, Bild aus
dem Store-Cover). Die Kachel startet über die Akeno-Payload am ELF-Loader direkt
`PPSA90410 --play=<id>`, ohne Umweg durch den Hub.

## Native PS5-Titel

Ports aus dem ProsperoStore und RecompStore sind selbst die Spiele. Akeno installiert, aktualisiert und
startet sie über die Payload am ELF-Loader, weil ein Titel keinen anderen Titel direkt starten kann.
