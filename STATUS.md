# Entwicklungsstand

Stand 27. November 2026, Version `01.002.000` (Title-ID `PPSA90410`). Für diesen Stand stand
**keine PS5** zur Verfügung. Alles unterhalb „Paketiert" ist daher offen, auch wenn Upstream-Projekte
dieselben Bausteine auf ihrer Hardware belegt haben.

Legende: ✅ erreicht · ⬜ offen · — nicht zutreffend

**Erster Konsolentest (Nutzer, 27. November 2026):** Der Titel erschien im Hauptmenü, der Start
endete mit **CE-107750-0**. Ursache im Build gefunden: Der gepinnte Modul-Konverter
(Boilerplate `470695e`) legte das RELRO-Segment auf den Datei-Offset der GOT statt auf
`.data.rel.ro`; Offset und Adresse waren bei 16-KiB-Seiten nicht kongruent. Behoben mit dem
korrigierten Konverter aus ProsperoStore `0e1f031`; `ps5/tools/check-module.py` weist solche Module
jetzt beim Build ab. Erneuter Konsolentest offen.

## Native PS5-App (`native/`, `ps5/`)

| Komponente | Implementiert | Kompiliert | Gelinkt | Paketiert | Auf PS5 gestartet | Spielbar / benutzbar | Online-MP verifiziert |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Titelordner `PPSA90410` mit `eboot.bin` (FSELF, signiert, Integrität gültig), `sce_module/libc.prx`, `sce_sys/` | ✅ | ✅ | ✅ | ✅ | ⬜ | ⬜ | — |
| Installations-ZIP `PPSA90410.zip` (Titelordner oben, wie ProsperoStore/ShadowMountPlus) | ✅ | — | — | ✅ | ⬜ | ⬜ | — |
| Oberfläche (SDL2, 1920×1080, DualSense/Tastatur) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host ✅ / PS5 ⬜ | — |
| HTTPS über `sceHttp` (Konsole) / libcurl (Host) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host ✅ / PS5 ⬜ | — |
| ProsperoStore (signierter Index, Ed25519, Rollback-Schutz) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host live ✅ | — |
| RecompStore (GitHub-Asset mit Digest) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host live: Katalog ✅, Auflösung ⬜ (GitHub-API in der Cloud gesperrt) | — |
| itch.io, Game Jolt, GOG, Epic (Entdecken) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host live ✅ | — |
| Download-Manager + Installer (SHA-256, ZIP-Prüfung, atomarer Tausch) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host live ✅ (DriftCheck, GyroAim real installiert) | — |
| Updates, „Alle aktualisieren", automatische Updates | ✅ | ✅ | ✅ | ✅ | ⬜ | Host-Tests ✅ | — |
| Deinstallation, Lauf-Schutz (`/mnt/sandbox`) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host-Tests ✅ | — |
| Titel starten (Payload über ELF-Loader 9021) | ✅ | ✅ | ✅ | ✅ | ⬜ | ⬜ | — |
| Hauptmenü-Verknüpfungen (Kachel-Titel `PPSA905xx`) | ✅ | ✅ | ✅ | ✅ | ⬜ | Host-Test (Ordner, `param.json`, Icon, `forward.txt`, Payload-Slot) ✅ / PS5 ⬜ | — |
| Engine-/Formaterkennung und Runtime-Routing | ✅ | ✅ | ✅ | ✅ | ⬜ | Host-Tests ✅ | — |

## Spiele und Runtimes über Akeno

Akeno installiert Titel aus den Katalogen. Ob ein Spiel startet oder online spielbar ist, belegen die
jeweiligen Upstream-Projekte für ihre Firmware; **Akeno selbst hat kein Spiel auf einer PS5 gestartet.**

| Beispiel | Über Akeno installierbar | Von Akeno auf PS5 gestartet | Upstream-Angabe |
| --- | --- | --- | --- |
| ProsperoCraft `PPSA99874` | ✅ (Host-Probe: Download-Daten aufgelöst) | ⬜ | Minecraft Java 26.x auf PS5, Microsoft-Konto oder Offline-Profil |
| PokeMMO Prospero `PPSA98001` | ✅ (im signierten Katalog) | ⬜ | FW 12.40, Online-Login und Kämpfe laut README |
| OpenRCT2 `PPSA99728` | ✅ (Host-Probe aufgelöst) | ⬜ | FW 13.00, 56–58 fps, eigene RCT2-Daten nötig |
| MihawkOS, PC5, Prospero Win (Windows) | ✅ | ⬜ | Alpha, spielabhängig |
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
| `make -C native test` (C++-Kern, inkl. RFC-8032-Vektoren, echtem signiertem Katalog, Installer-Ende-zu-Ende mit Fake-Transport) | 347 Prüfungen, 0 Fehler |
| `make -C native probe` (live, alle Stores) | 6/6 Stores, 412 Einträge, 256 installierbar |
| Live-Installation auf dem Host (`akeno-probe install:PPSA98766 install:PPSA99510`) | beide installiert, Bibliothek erkennt sie |
| `python3 -m unittest discover -s tests` | 52 Tests, 3 Live-Tests übersprungen |
| Live: `AKENO_LIVE_STORES=prosperostore,itch` | grün |
| `bash ps5/build.sh` | `eboot.bin` und `libc.prx` „integrity: valid", Payload besteht ProsperoStores Loader-ELF-Prüfung |

Nächster Schritt ist der Konsolentest nach [INSTALLATION.md](INSTALLATION.md#erster-test-auf-der-ps5).
