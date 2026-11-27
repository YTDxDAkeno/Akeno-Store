# Build und Tests

Erforderlich sind Linux und Python 3.12 oder neuer. Die Laufzeit benötigt keine zusätzlichen Python-Bibliotheken. Die Make-Ziele setzen GNU Make voraus.

```sh
make test
make build
```

Der Build erzeugt `dist/akeno-gaming-hub-host.zip`. Bei identischen Eingabedateien und identischer ZIP-Komprimierungsimplementierung erzeugt er identische Bytes. Das ZIP enthält den lauffähigen Host-Quellcode. Entpacke es und starte `python3 -m core.server --offline` im enthaltenen Verzeichnis.

Die Tests decken API, Katalog-Fixtures, echte Schleifenanfragen an einen HTTP-Testserver sowie Archiv- und Installationsfehler ab. Für manuelle Browserprüfung siehe [Installation](INSTALLATION.md). Für die Zielplattform siehe [PS5-Plattform](platform/ps5/README.md).

Der optionale Browser-Smoke-Test benötigt Playwright für Python und Chromium. Mit einem installierten Chromium:

```sh
CHROMIUM=/usr/bin/chromium python3 -m tools.browser_smoke
```

Der Test arbeitet in einem temporären Verzeichnis. Er installiert Signal Drift über die Oberfläche, öffnet es im Player, löst das Rätsel und entfernt das Spiel wieder.

## Native SDL-Oberfläche

Installiere einen C11-Compiler, GNU Make, pkg-config und SDL2-Entwicklungsdateien. Unter Debian oder Ubuntu heißen die Pakete `build-essential`, `pkg-config` und `libsdl2-dev`.

```sh
make native-test
make native-catalog CATALOG_ARGS=--offline
dist/native/akeno-hub dist/native/data/catalog.tsv dist/native/data/library.tsv
```

Der optionale Offline-Schalter überspringt öffentliche Feeds. Der Exporter übernimmt den lokalen Bibliotheksstand aus demselben `--data-dir` wie der Host-Hub. Er schreibt die verfügbaren Daten auch beim Ausfall eines einzelnen Providers und meldet diesen Fehler am Prozessende. Starte die SDL-Anwendung nach dem nächsten Export erneut, um die Daten neu zu laden.

## PS5 und Windows

Der [PS5-Build](platform/ps5/README.md) benötigt einen Zielcompiler und die Zielversion von SDL2. Ein Windows-PC kann denselben Build mit WSL ausführen. Gib eine vorhandene WSL-Distribution und den Checkout-Pfad innerhalb von WSL an.

```powershell
.\build\build-wsl.ps1 -Distribution Ubuntu -Checkout /mnt/c/repos/Akeno-Store
.\build\build-wsl.ps1 -Distribution Ubuntu -Checkout /mnt/c/repos/Akeno-Store -Target ps5 -SdkConfig /opt/akeno/sdk.mk
```
