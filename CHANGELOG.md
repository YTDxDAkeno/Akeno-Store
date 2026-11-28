# Änderungen

## 01.004.000 – Architekturkorrektur: Spiele laufen in Akeno selbst

- **Keine Weiterleitungen mehr:** Spielen über Parallax Browser, ProsperoWeb, MihawkOS, RetroArch oder
  andere Launcher ist entfernt, ebenso die Installation in MihawkOS. Spiele ohne eingebaute
  Akeno-Runtime sind als „Nicht kompatibel" gekennzeichnet und nennen die fehlende Runtime.
- **Spielmodus:** Akeno startet seinen eigenen Titel mit `--play=<id>` neu
  (`sceSystemServiceLoadExec`); die Runtime spielt im Vollbild und Akeno kehrt danach in den Hub zurück.
- **Erste eingebaute Runtime:** EasyRPG Player 0.8.1.1 für RPG Maker 2000/2003, aus gepinnten Quellen
  gebaut und in Akenos `eboot.bin` gelinkt (dazu liblcf, pixman, libpng, zlib, fmt, inih). Fehlende
  C-Locale-Funktionen für libc++ ergänzt `ps5/runtime/libc.c`.
- **Akeno-Bibliothek:** Download der originalen Spieldateien, Erkennung von Engine und Runtime anhand
  der Dateien, Startprofil in `/data/akeno/library/<id>`. Unpassende Downloads werden nicht installiert.
  Eigene Spiele aus `/data/akeno/games` lassen sich ohne Kopie spielen.
- **itch.io:** RPG-Maker-2000/2003-Spiele aus den itch.io-Feeds lassen sich über die offizielle
  itch.io-API mit dem eigenen API-Schlüssel installieren (`data.json` → Uploads → signierte
  Download-Adresse; der Schlüssel geht nur an `api.itch.io`). Schlüssel über Datei oder
  Bildschirmtastatur.
- **Hauptmenü-Kacheln pro Spiel**, die Akeno direkt im Spielmodus starten.
- **Cover:** keine Buchstaben-Platzhalter mehr; fehlt ein Bild, steht „Kein Cover vom Anbieter"
  daneben. Fehlgeschlagene Bilder werden bis zu dreimal neu geladen; die Diagnose zeigt den letzten
  Bildfehler. Offizielle Logos aus den Projekt-Repositories für kuratierte Einträge.
- **Runtime-Manager** zeigt die Akeno-Runtimes mit Stand und Grundlage; Recherche dazu in
  `docs/RUNTIME_RESEARCH.md`.
- Installer: Ein abgeschlossener Download mit falscher Größe wird nicht mehr dreimal wiederholt;
  Zugangsdaten werden bei Weiterleitungen auf andere Hosts entfernt.

## 01.003.000 – Spiele aus dem Master-Prompt, MihawkOS-Installationen, Kompatibilität, Diagnose

- **Downloads auf der Konsole repariert:** `sceHttp 0x80431073` (Antwort-Header zu groß bei
  GitHub-Weiterleitungen) mit größerer Header-Grenze behoben; abgebrochene Downloads werden bis zu
  dreimal per HTTP-Range fortgesetzt; verständliche Fehlertexte für sceHttp-Codes (der DNS-Fall griff
  wegen einer falschen Bitmaske nie und ist korrigiert).
- **Akeno-Katalog:** 38 kuratierte kostenlose, Online- und Fanspiele aus Abschnitt 9, jedes mit
  tatsächlichem Weg auf die PS5 und ehrlichem Kompatibilitätsstatus; native Ports ergänzen den
  signierten ProsperoStore-Eintrag um Online-, Crossplay- und Steuerungsangaben. Der Katalog liegt
  im Titel und kann durch eine neuere Revision aus dem Repository ersetzt werden.
- **Browserspiele starten direkt:** Parallax Browser (`PPSA99207`) übernimmt die Spieladresse als
  Startargument (lange Adressen über `/data/ps5webkit/open-url.txt`); ProsperoWeb bleibt mit QR-Code.
- **Windows-Spiele in MihawkOS:** Akeno lädt freie Windows-Builds nur mit von GitHub veröffentlichtem
  SHA-256, entpackt sie nach `C:\Games\<Name>`, legt den Bibliothekseintrag
  `/data/ps5proton/games/<id>/game.json` an und startet sie mit `profile=<id>`. Fremde Einträge werden
  nie überschrieben oder entfernt; die Bibliothek zeigt auch Spiele, die in MihawkOS angelegt wurden.
- **Kompatibilitätsdatenbank:** eigene Testberichte (Gesamtstatus plus Start, Gameplay, Grafik, Ton,
  Controller, Online), Nachfrage nach einem Spielstart aus Akeno, Export und Import als JSON; nichts
  wird gesendet.
- **Diagnose-Tab:** System und Firmware, Speicherorte, Runtimes, letzter Start über den ELF-Loader,
  Store-Zustände, Netzwerktest zu allen Stores, Protokoll; „Bericht speichern" schreibt
  `/data/akeno/diagnose.txt`.
- **Neue Bereiche:** Kostenlos, Online & Multiplayer, Singleplayer, Fanprojekte, Indie, Windows
  (MihawkOS), Getestet; neue Regale auf „Entdecken"; Runtimes TrinityXSX und Parallax Browser.
- Übersicht über den Stand jedes Master-Prompt-Abschnitts: `docs/MASTER_PROMPT_COVERAGE.md`.

## 01.002.000 – Korrektur nach dem ersten Konsolentest

- Start schlug mit CE-107750-0 fehl: Das RELRO-Segment des Moduls hatte einen Datei-Offset, der nicht
  zur Adresse passte (älterer Modul-Konverter). Korrigierter Konverter aus ProsperoStore `0e1f031`,
  neue Build-Prüfung `ps5/tools/check-module.py`.
- ZIP enthält Verzeichniseinträge mit Rechten `0777`; Version im Schema `01.xxx.xxx`.

## 01.002.000 – Native PS5-Titel (27. November 2026)

- Neuer nativer C++20-Hub (SDL2) mit dunkelblau-violettem Design, großen Covern, Hero-Banner,
  Regalen, Store-Raster mit Filtern, Bildschirmtastatur, Detailseiten, QR-Codes, Toasts und
  DualSense-Glyphen; identischer Code für Host und PS5.
- PS5-Build mit öffentlichen Werkzeugen: signiertes `eboot.bin`, `libc.prx`, `sce_sys`,
  Installations-ZIP `PPSA90410.zip`; GitHub-Actions-Artefakt.
- Stores: vollständiger signierter ProsperoStore-Katalog (Ed25519, Rollback-Schutz), RecompStore
  (GitHub-Digest), itch.io, Game Jolt, GOG, Epic.
- Download-Manager und Installer mit SHA-256, ZIP-Prüfung, atomarem Tausch, Updates, automatischen
  Updates, Deinstallation und Lauf-Schutz.
- Runtime-Router für ROMs, Disc-Images, Windows-, Flash-, J2ME- und HTML5-Inhalte zu vorhandenen
  PS5-Runtime-Titeln; Starts und Hauptmenü-Verknüpfungen über den ELF-Loader.
- Audit der Codex-Basis ([docs/AUDIT.md](docs/AUDIT.md)); überholte PS5-Pipeline entfernt;
  Python-Host nutzt jetzt ebenfalls den signierten Katalog.

## 0.1.0 – Host-Entwicklungsbuild (Codex)

- Lokaler Web-Hub mit Katalog, Bibliothek, HTML5-Player, Signal Drift und Kompatibilitätsdatenbank.
