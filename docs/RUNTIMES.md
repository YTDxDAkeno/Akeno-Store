# Universelle Runtimes

Akeno portiert keine Spiele einzeln. Jede „Runtime" ist ein vorhandener PS5-Titel aus dem signierten
ProsperoStore-Katalog. Akeno erkennt das Format eigener Dateien, wählt die passenden Runtime-Titel,
installiert sie wie jeden anderen Titel und startet sie.

## Erkennung (`native/src/runtime/runtimes.cpp`)

Gelesen werden nur Dateiköpfe, nie ausgeführt. Beispiele: iNES-Kopf, N64-Byte-Order, Game-Boy-Prüfsumme,
GBA-Header, NDS-Logo-Prüfsumme, GameCube/Wii-Disc-Magic, XDVDFS (Xbox, Xbox 360 XGD2/3),
ISO 9660 mit `PLAYSTATION`-System und `BOOT2` (PS2) bzw. ohne (PS1), PSP-Volume, XCI/NSP, SWF, J2ME-Manifest,
PE (x86/x86-64), ELF, Godot-PCK, Ren'Py, RPG-Maker-Projekte, HTML5-Exporte (Godot, Unity WebGL, Ren'Py).

## Routing

| Familie | Runtime-Titel (Reihenfolge) | Startargumente |
| --- | --- | --- |
| Web / HTML5, Unity WebGL, Godot Web, RPG Maker MV/MZ | ProsperoWeb `PRWB00001` | keine dokumentierte Start-URL → Akeno zeigt die Adresse (QR) |
| Windows (PE) | MihawkOS `PPSA99170`, PC5 `PPSA11018`, Prospero Win `PPSA99995` | MihawkOS: `path=<Windows-Pfad>` (in `proton_launch.h` dokumentiert, Konsolentest offen) |
| NES, SNES, GB/GBA, Mega Drive | RetroArch `PPSA99169`, Genesis Plus GX, Snes9x, fceuxs5 | keine (Runtime-eigene Bibliothek) |
| N64 | Mupen64Plus `PPSA99064`, PSGopher, RetroArch | keine |
| PS1 / PS2 / PSP | PSXS5, SwanStation / PS5SX2, Crux2 / PSP5 | keine |
| GameCube/Wii, Dreamcast, Saturn | Porpoise, PSFlyCast, YmirPS5 | keine |
| Xbox / Xbox 360 / Switch | XPSemu / PS5X360 / ProsperoEden | keine |
| Flash, J2ME, PICO-8, C64 | Ruffle, Capy Mobile, PICO-8 for PS5, PS64 | keine |

Ohne PS5-Runtime im Katalog (Stand heute): allgemeine Java-Desktop-JARs (ProsperoCraft bringt Java 25
nur für Minecraft mit), Linux-ELF, native Ren'Py/Godot/Unity-Builds, RPG Maker 2000/2003, .NET, Python,
Lua/LÖVE. Akeno nennt dann die Alternative (Web-Export → ProsperoWeb, Windows-Build → MihawkOS).

## Starten und Verknüpfungen

Ein Titel darf keinen anderen starten. Akeno schreibt deshalb `/data/akeno/launch/PPSA90410.txt`
(`AKENO-LAUNCH 1`, Ziel-Title-ID, Argumente) und sendet `assets/payload/akeno-launch.elf` an den
ELF-Loader auf Port 9021; die Payload wartet, bis Akeno geschlossen ist, und ruft
`sceLncUtilLaunchApp` (Fallback `sceSystemServiceLaunchApp`) auf. Hauptmenü-Verknüpfungen sind kleine
Titelordner `PPSA90500`–`PPSA90599` mit eigener Kachel, `forward.txt` und derselben Payload. Das Design
stammt von den Forwarder-Kacheln von PS5_RetroArch und MihawkOS (GPL-3.0-or-later). Konsolentest offen.

## Eigene ROMs und ISOs

Eigene, rechtmäßig erstellte Dumps gehören nach `/data/akeno/games` (oder `/mnt/usbN/akeno/games`).
Recomps aus RecompStore nennen das benötigte Original (z. B. „US-Disc SLUS-21590"); die Einrichtung
übernimmt das jeweilige Spiel. Akeno liefert keine geschützten Spieldaten.

## Python-Host-Runner

`runtimes/managed.py` (Java 17/21/25, Linux-ELF auf dem Entwicklungsrechner) bleibt als Testwerkzeug
erhalten; es hat keine Funktion auf der PS5.
