# Entwicklungsstand

Stand 27. November 2026, Version `01.003.000` (Title-ID `PPSA90410`). Die Abdeckung des
Master-Prompts Punkt für Punkt: [docs/MASTER_PROMPT_COVERAGE.md](docs/MASTER_PROMPT_COVERAGE.md).

Legende: ✅ erreicht · ⬜ offen · — nicht zutreffend

**Konsolentests (Nutzer, 27. November 2026):**

1. `01.002.000` (erster Build) endete mit **CE-107750-0**: Der gepinnte Modul-Konverter legte das
   RELRO-Segment falsch. Behoben mit dem korrigierten Konverter aus ProsperoStore `0e1f031`;
   `ps5/tools/check-module.py` weist solche Module beim Build ab.
2. Der korrigierte Build **startete auf der PS5**: Oberfläche, Kataloge und Download-Warteschlange
   erschienen (Foto des Nutzers). **Jede Installation scheiterte** mit `sceHttp 0x80431073` (Antwort-Header
   zu groß – GitHub-Weiterleitungen tragen lange Header). Behoben: größere Header-Grenze,
   Wiederaufnahme abgebrochener Downloads. Erneuter Test mit `01.003.000` offen.

## Native PS5-App (`native/`, `ps5/`)

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
| Engine-/Formaterkennung und Runtime-Routing | ✅ | ✅ | ✅ | ✅ | ⬜ | Host-Tests ✅ | — |
| Akeno-Katalog (38 kuratierte Spiele, mitgeliefert + neuere Revision aus dem Repository) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host live ✅ | — |
| Browserspiele direkt in Parallax Browser (Startadresse als Argument) | ✅ | ✅ | ✅ | ✅ | ⬜ | ⬜ | — |
| Windows-Spiele in MihawkOS installieren (`C:\Games`, `games/<id>/game.json`) und mit `profile=` starten | ✅ | ✅ | ✅ | ✅ | ⬜ | Host-Test (Fake-Download, Layout, Deinstallation) ✅ / PS5 ⬜ | — |
| Kompatibilitätsdatenbank (eigene Testberichte, Nachfrage nach dem Spielen, Export/Import) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host-Tests ✅ | — |
| Diagnose-Tab (System, Speicher, Runtimes, letzter Start, Stores, Netzwerktest, Protokoll, Bericht) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host ✅ | — |

## Spiele und Runtimes über Akeno

Akeno installiert Titel aus den Katalogen. Ob ein Spiel startet oder online spielbar ist, belegen die
jeweiligen Upstream-Projekte für ihre Firmware; **Akeno selbst hat kein Spiel auf einer PS5 gestartet.**

| Beispiel | Über Akeno installierbar | Von Akeno auf PS5 gestartet | Upstream-Angabe |
| --- | --- | --- | --- |
| ProsperoCraft `PPSA99874` | ✅ (Host-Probe: Download-Daten aufgelöst) | ⬜ | Minecraft Java 26.x auf PS5, Microsoft-Konto oder Offline-Profil |
| PokeMMO Prospero `PPSA98001` | ✅ (im signierten Katalog) | ⬜ | FW 12.40, Online-Login und Kämpfe laut README |
| OpenRCT2 `PPSA99728` | ✅ (Host-Probe aufgelöst) | ⬜ | FW 13.00, 56–58 fps, eigene RCT2-Daten nötig |
| MihawkOS, PC5, Prospero Win, TrinityXSX (Windows) | ✅ | ⬜ | Alpha, spielabhängig |
| Parallax Browser (WebGL, WebGPU, Gamepad-API) | ✅ | ⬜ | Startadresse als Argument laut Quellcode und README des Projekts auf der Konsole geprüft |
| ProsperoWeb (HTML5/WebGL) | ✅ | ⬜ | FW 13.42 laut README |
| RetroArch und Einzel-Emulatoren | ✅ | ⬜ | je Projekt |

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
| `make -C native test` (C++-Kern, inkl. RFC-8032-Vektoren, echtem signiertem Katalog, Installer-Ende-zu-Ende mit Fake-Transport, Wiederaufnahme, Akeno-Katalog, MihawkOS-Installation, Kompatibilitätsberichte) | 490 Prüfungen, 0 Fehler |
| `make -C native probe` (live, alle Stores) | 7/7 Kataloge, 442 Einträge nach dem Zusammenführen, 259 installierbar, 45 in einer Runtime startbar |
| Live-Installation auf dem Host (`akeno-probe install:PPSA98766 install:PPSA99510`) | beide installiert, Bibliothek erkennt sie |
| `python3 -m unittest discover -s tests` | 52 Tests, 3 Live-Tests übersprungen |
| Live: `AKENO_LIVE_STORES=prosperostore,itch` | grün |
| `bash ps5/build.sh` | `eboot.bin` und `libc.prx` „integrity: valid", Payload besteht ProsperoStores Loader-ELF-Prüfung |

Nächster Schritt ist der Konsolentest von `01.003.000` nach [INSTALLATION.md](INSTALLATION.md#erster-test-auf-der-ps5).
Bei Problemen hilft der Diagnosebericht (Tab „Diagnose" → „Bericht speichern" → `/data/akeno/diagnose.txt`).
