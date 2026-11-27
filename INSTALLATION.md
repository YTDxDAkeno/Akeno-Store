# Installation

## Auf der PS5

Voraussetzungen (dieselben wie für ProsperoStore):

- eine Homebrew-fähige PS5 mit [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus)
  **1.7beta4 oder neuer** (registriert Titelordner im Hauptmenü und bindet `/data` in die App ein),
- für das Starten anderer Titel aus Akeno und für Hauptmenü-Verknüpfungen: ein ELF-Loader auf
  Port 9021 (etaHEN/kstuff-Umgebung),
- korrektes Datum und Uhrzeit (sonst schlagen HTTPS-Verbindungen fehl).

Schritte:

1. `PPSA90410.zip` entpacken (aus `ps5/dist/`, dem GitHub-Actions-Artefakt oder selbst gebaut, siehe
   [BUILD.md](BUILD.md)).
2. Den Ordner `PPSA90410` nach `/data/homebrew/` kopieren, sodass `/data/homebrew/PPSA90410/eboot.bin`
   existiert.
3. Rechte des Ordners rekursiv auf `777` setzen, falls das FTP-Programm sie nicht setzt (sonst
   meldet die Konsole CE-107750-0). Der FTP-Payload `ftpsrv` setzt sie automatisch.
4. Einige Sekunden warten, bis ShadowMountPlus die Kachel „Akeno Gaming Hub" anlegt, dann starten.

Akeno speichert Einstellungen, Caches, Belege und das Protokoll in `/data/akeno` (ohne `/data`-Zugriff
in `/download0/akeno`; dann ist nur Stöbern möglich). Installierte Titel landen im gewählten
ShadowMountPlus-Ordner (Standard `/data/homebrew/<TITLE-ID>`), die jeweils vorige Version in
`/data/akeno-work/previous`.

### Erster Test auf der PS5

Dieser Ablauf trennt die Stufen, damit klar ist, was belegt ist. Bitte nach jedem Schritt notieren,
was passiert, und bei Problemen `/data/akeno/akeno.log` (und `akeno.prev.log`) per FTP mitschicken.

| # | Schritt | Erwartung | Belegt dann |
| --- | --- | --- | --- |
| 1 | Akeno aus dem Hauptmenü starten | Startbild, danach Akeno-Oberfläche in 1080p | Start auf PS5 |
| 2 | Steuerkreuz, Stick, Kreuz, Kreis, L1/R1, Dreieck, Options | Fokus bewegt sich, Tabs wechseln, Suche öffnet die Bildschirmtastatur | DualSense-Navigation |
| 3 | Oben rechts „x/6 Stores online" abwarten | ProsperoStore, RecompStore, itch.io, Game Jolt, GOG, Epic laden; Cover erscheinen | HTTPS über `sceHttp` auf der Konsole |
| 4 | Einstellungen → Installationsort | „Beschreibbar" | `/data`-Zugriff über ShadowMountPlus |
| 5 | Kleine App installieren, z. B. „DriftCheck" (126 KB) | Downloads-Tab: Download, Prüfung, Entpacken, Fertig; Kachel erscheint im Hauptmenü | Installation |
| 6 | DriftCheck im Hauptmenü starten | App startet | installierter Titel lauffähig |
| 7 | Bibliothek → DriftCheck → Starten (Akeno schließt sich) | DriftCheck startet; `/data/akeno/launch/last.log` zeigt `sceLncUtilLaunchApp = 0x00000000` | Start über ELF-Loader |
| 8 | Bibliothek → DriftCheck → Deinstallieren | Ordner und Kachel verschwinden (die Kachel kann bis zum Neustart von ShadowMountPlus bleiben) | Deinstallation |
| 9 | Eigene Datei (z. B. ein selbst gedumptes NES-ROM) nach `/data/akeno/games` kopieren | Bibliothek → „Eigene Spieldateien" erkennt das Format und bietet RetroArch an | Formaterkennung auf der Konsole |

Für Online-Spiele (z. B. PokeMMO Prospero) gilt zusätzlich: Login, Verbindung zum Server und
Mehrspieler müssen im jeweiligen Spiel geprüft werden; Akeno belegt nur die Installation.

## Auf dem Entwicklungsrechner (Linux)

```sh
make native-run
```

Der Host-Build verwendet dieselben Stores und denselben Installer; installiert wird in einen
simulierten Baum unter `~/.local/share/akeno-native/simulated-ps5/data/homebrew`
(`AKENO_DATA_DIR` ändert den Ort). Starten von Titeln ist auf dem Host nicht möglich.

Der ältere Python-Web-Hub (HTML5-Player, Signal Drift) läuft weiterhin mit
`python3 -m core.server --offline --data-dir /tmp/akeno-hub`.
