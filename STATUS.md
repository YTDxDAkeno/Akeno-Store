# Entwicklungsstand

Stand 28. November 2026, Version `01.004.000` (Title-ID `PPSA90410`). Die Abdeckung des
Master-Prompts Punkt für Punkt: [docs/MASTER_PROMPT_COVERAGE.md](docs/MASTER_PROMPT_COVERAGE.md).

Legende: ✅ erreicht · ⬜ offen · — nicht zutreffend

**Architektur seit `01.004.000`:** Spiele laufen in Akeno selbst. Akeno startet seinen eigenen Titel
im Spielmodus neu, eine eingebaute Runtime spielt das Spiel. Weiterleitungen an andere Apps
(Browser, MihawkOS, Emulator-Apps) gibt es nicht mehr ([docs/RUNTIMES.md](docs/RUNTIMES.md)).

**Konsolentests (Nutzer, 27. November 2026):**

1. `01.002.000` (erster Build) endete mit **CE-107750-0**: Der gepinnte Modul-Konverter legte das
   RELRO-Segment falsch. Behoben mit dem korrigierten Konverter aus ProsperoStore `0e1f031`;
   `ps5/tools/check-module.py` weist solche Module beim Build ab.
2. Der korrigierte Build **startete auf der PS5**: Oberfläche, Kataloge und Download-Warteschlange
   erschienen (Foto des Nutzers). **Jede Installation scheiterte** mit `sceHttp 0x80431073` (Antwort-Header
   zu groß – GitHub-Weiterleitungen tragen lange Header). Behoben in `01.003.000`, auf der Konsole noch
   nicht bestätigt.

## Native PS5-App (`native/`, `ps5/`)

| Komponente | Implementiert | Kompiliert | Gelinkt | Paketiert | Auf PS5 gestartet | Spielbar / benutzbar | Online-MP verifiziert |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Titelordner `PPSA90410` mit `eboot.bin` (FSELF, signiert, Integrität gültig), `sce_module/libc.prx`, `sce_sys/` | ✅ | ✅ | ✅ | ✅ | ✅ (Nutzer, `01.002.000`) | ✅ | — |
| Installations-ZIP `PPSA90410.zip` (Titelordner oben, wie ProsperoStore/ShadowMountPlus) | ✅ | — | — | ✅ | ✅ | ✅ | — |
| Oberfläche (SDL2, 1920×1080, DualSense/Tastatur) | ✅ | ✅ | ✅ | ✅ | ✅ | Host ✅ / PS5: Anzeige ✅, Bedienung laut Nutzer ✅, Bildrate ungemessen | — |
| HTTPS über `sceHttp` (Konsole) / libcurl (Host) | ✅ | ✅ | ✅ | ✅ | ✅ | Host ✅ / PS5: Kataloge ✅, Downloads ⬜ (0x80431073 in `01.002.000`, Korrektur ungetestet) | — |
| ProsperoStore (signierter Index, Ed25519, Rollback-Schutz) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host live ✅ | — |
| RecompStore (GitHub-Asset mit Digest) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host live: Katalog ✅, Auflösung ⬜ (GitHub-API in der Cloud gesperrt) | — |
| itch.io, Game Jolt, GOG, Epic (Entdecken) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host live ✅ | — |
| Download-Manager + Installer (SHA-256, ZIP-Prüfung, atomarer Tausch, Wiederaufnahme, 3 Versuche) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host live ✅ (DriftCheck, GyroAim real installiert) | — |
| Updates, „Alle aktualisieren", automatische Updates | ✅ | ✅ | ✅ | ✅ | ⬜ | Host-Tests ✅ | — |
| Deinstallation, Lauf-Schutz (`/mnt/sandbox`) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host-Tests ✅ | — |
| Titel starten (Payload über ELF-Loader 9021) | ✅ | ✅ | ✅ | ✅ | ⬜ | ⬜ | — |
| Hauptmenü-Verknüpfungen (Kachel-Titel `PPSA905xx`) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host-Test (Ordner, `param.json`, Icon, `forward.txt`, Payload-Slot) ✅ / PS5 ⬜ | — |
| Engine-Erkennung (Engine, Runtime, spielbar ja/nein) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host-Tests ✅ | — |
| **Spielmodus** (Neustart mit `--play=<id>`, Rückkehr mit `--return=<id>`) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host ✅ (`execv`) / PS5 ⬜ (`sceSystemServiceLoadExec`, wie Prospero Win) | — |
| **Eingebaute RPG-Maker-2000/2003-Runtime** (EasyRPG Player 0.8.1.1 im `eboot.bin`) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host: EasyRPG-TestGame läuft im Spielmodus ✅ / PS5 ⬜ | — |
| Akeno-Bibliothek: Download, Engine-Erkennung, Startprofil, Deinstallation | ✅ | ✅ | ✅ | ✅ | ⬜ | Host-Tests (Fake-Download) ✅ | — |
| itch.io-Downloads über die offizielle API mit eigenem Schlüssel | ✅ | ✅ | ✅ | ✅ | ⬜ | Parser-Tests ✅ / mit echtem Schlüssel ⬜ | — |
| Hauptmenü-Kachel pro Spiel (startet `PPSA90410 --play=<id>`) | ✅ | ✅ | ✅ | ✅ | ⬜ | ⬜ | — |
| Akeno-Katalog (38 kuratierte Spiele, mitgeliefert + neuere Revision aus dem Repository) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host live ✅ | — |
| Kompatibilitätsdatenbank (eigene Testberichte, Nachfrage nach dem Spielen, Export/Import) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host-Tests ✅ | — |
| Diagnose-Tab (System, Speicher, Runtimes, letzter Start, Stores, Netzwerktest, Protokoll, Bericht) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host ✅ | — |

## Spiele und Runtimes

| Was | In Akeno spielbar | Auf der PS5 belegt |
| --- | --- | --- |
| Native PS5-Titel aus ProsperoStore/RecompStore (z. B. PokeMMO Prospero, ProsperoCraft, SuperTuxKart) | ✅ (installieren, starten) | Upstream-Projekte belegen ihre Titel; Start aus Akeno ⬜ |
| RPG Maker 2000/2003 (itch.io, eigene Ordner) | ✅ eingebaute Runtime | ⬜ |
| Windows, Linux, Java, HTML5, Godot, Unity, Ren'Py, .NET, ROMs | ⬜ geplant, als „Nicht kompatibel" markiert | — |

## Host-Entwicklungs-Hub (Python, `core/`, `stores/`, `apps/ps5-hub/web`)

Weiterhin lauffähig: lokaler Web-Hub mit HTML5-Player, Signal Drift, geprüften ZIP-Installationen und
Kompatibilitätsdatenbank. 52 Tests grün; ProsperoStore jetzt mit vollständigem signiertem Index.

## Leistung des Software-Renderers

Die PS5-SDL hat nur den Software-Renderer. Gemessen mit demselben Renderpfad auf dem Linux-Host
(`SDL_RENDER_DRIVER=software`, geteilter Cloud-Xeon 2,3 GHz, 1920×1080): nach der Optimierung
(vorgebackener Hintergrund und Effekte, gecachte Textzeilen, deckende Flächen ohne Blending,
pixelgenaue Ecken) 16 ms pro Frame in allen Tabs, 20–23 ms auf textlastigen Detailseiten; vorher
26–56 ms. Das ist ein Anhaltspunkt, **keine Messung auf der PS5**.

## Tests

| Suite | Ergebnis |
| --- | --- |
| `make -C native test` (C++-Kern, inkl. RFC-8032-Vektoren, echtem signiertem Katalog, Installer-Ende-zu-Ende mit Fake-Transport, Wiederaufnahme, Bibliotheks-Installation, Erkennung, itch.io-API, Spielmodus-Argumente, Kompatibilitätsberichte) | 513 Prüfungen, 0 Fehler |
| Spielmodus auf dem Host (`akeno --play=testgame-2000`, EasyRPG-TestGame 2000, Xvfb) | Titel- und Sprachwahl-Bildschirm gerendert, Ende mit Code 0 |
| `make -C native probe` (live, alle Stores) | 7/7 Kataloge, 517 Einträge nach dem Zusammenführen |
| Live-Installation auf dem Host (`akeno-probe install:PPSA98766 install:PPSA99510`) | beide installiert, Bibliothek erkennt sie |
| `python3 -m unittest discover -s tests` | 52 Tests, 3 Live-Tests übersprungen |
| Live: `AKENO_LIVE_STORES=prosperostore,itch` | grün |
| `bash ps5/build.sh` | `eboot.bin` (mit EasyRPG, 8,9 MB) und `libc.prx` „integrity: valid", Modulprüfung bestanden |

Nächster Schritt ist der Konsolentest von `01.004.000` nach [INSTALLATION.md](INSTALLATION.md#erster-test-auf-der-ps5).
Bei Problemen hilft der Diagnosebericht (Tab „Diagnose" → „Bericht speichern" → `/data/akeno/diagnose.txt`).
