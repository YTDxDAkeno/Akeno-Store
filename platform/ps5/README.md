# Akeno Gaming Hub 0.1.0 – native PS5 Alpha

Die native C-Oberfläche verwendet SDL2, liest Katalog und Bibliothek und unterstützt Suche, Plattformfilter und Detailansicht. Die zwei C-Dateien wurden mit einem PS5-Sysroot zu einem gemeinsamen ELF-Modul gelinkt. Für den Start auf der Konsole fehlt ein `eboot.bin` aus einer autorisierten Anwendungsumgebung.

## Lokaler Build unter Linux und WSL

Für den Zielmodul-Build sind x86-64-Linux, GNU Make, Clang 19 und LLD 19, Bash, tar, curl, coreutils und Python 3.11+ erforderlich. Der Export des Katalogs benötigt Python 3.12. Auf Debian 13 sind die LLVM-Pakete `clang-19` und `lld-19`.

```sh
bash build/setup-toolchain.sh
bash build/build-ps5.sh module
bash build/validate-ps5.sh module dist/ps5/akeno-ui.so
sha256sum dist/ps5/akeno-ui.so
```

Das Setup verifiziert PacBrew v0.40.2 gegen den gepinnten SHA-256 und extrahiert ausschließlich die verwendeten Header, Bibliotheksarchive und Linkerimporte. Standardpfad ist `.cache/akeno-ps5`. Das gelinkte Modul hat den Typ ELF64 DYN und keine Startadresse. SDL2, libiconv und libsamplerate sind statisch eingebunden. Die angezeigten Systemimporte stammen aus dem Ziel-Sysroot.

Unter Windows führt PowerShell denselben Ablauf in einer bestehenden WSL-Distribution aus.

```powershell
.\build\setup-wsl.ps1 -Distribution Debian -Checkout /mnt/c/repos/Akeno-Store
.\build\build-ps5.ps1 -Distribution Debian -Checkout /mnt/c/repos/Akeno-Store
```

## Autorisierte Anwendung paketieren

Eine vorhandene SDK-Integration lässt sich über `bash build/build-ps5.sh sdk /pfad/zur/sdk.mk` ansprechen. Darin setzt das Ziel-SDK `PS5_CC`, `PS5_CPPFLAGS`, `PS5_CFLAGS`, `PS5_SDL2_CFLAGS`, `PS5_LDFLAGS` und `PS5_SDL2_LIBS`. Das SDK erzeugt den autorisierten Anwendungsordner mit `eboot.bin`, Runtime-Modulen und `sce_sys/param.json`. Akenos Projektkennung lautet `org.akenogaming.hub`. Die davon unabhängige Konsolen-Title-ID kommt aus den vom SDK vergebenen Metadaten.

```sh
python3 -m core.native_catalog --offline --output dist/native/data
python3 -m build.ps5_release assets /pfad/zum/sdk-app-ordner dist/native/data
bash build/validate-ps5.sh app /pfad/zum/sdk-app-ordner --title-id "$AKENO_SDK_TITLE_ID"
bash build/package-ps5.sh /pfad/zum/sdk-app-ordner dist/ps5/akeno-app.zip --title-id "$AKENO_SDK_TITLE_ID"
bash build/validate-ps5.sh archive dist/ps5/akeno-app.zip
```

Setze `AKENO_SDK_TITLE_ID` auf die tatsächliche Title-ID aus `param.json`. Der Assets-Schritt kopiert Akenos 512×512-Icon und die beiden TSV-Dateien. Das Archiv enthält einen nach der Title-ID benannten Ordner und ein Manifest mit SHA-256 und Länge jeder Datei. Das Skript prüft Ordnerstruktur, Title-ID und Dateiköpfe. Signatur, Laufzeitkompatibilität und Installation prüft der autorisierte Veröffentlichungsweg des SDKs.

## Status und Hardwareprüfung

| Stufe | Stand |
| --- | --- |
| Quellcode | Nativer SDL2-Katalog, Plattformfilter, Suche und Bibliothek implementiert |
| Kompiliert | Gegen PacBrew-Zielheader mit Clang 19 geprüft |
| Gelinkt | ELF64-DYN-Modul mit SDL2 und geprüften Linkerimporten |
| Vollständig paketiert | Ein für Akeno autorisiertes `eboot.bin` und der dazugehörige Runtime-Satz fehlen |
| Auf echter PS5 gestartet | Offen |
| Gameplay verifiziert | Offen |
| Multiplayer verifiziert | Offen |

Für den Konsolentest werden SDK-Version, Firmware, Konsole und Archivhash festgehalten. Zuerst Installation und Einstiegspunkt prüfen. Danach Bildausgabe, Auflösung, Controller, Hotplug, Suche, Bibliotheks-Snapshot und sauberes Beenden testen. Installation von Spielen erst nach einem verfügbaren gerätespezifischen Installations-API prüfen. Dabei sind Netzwerkabbruch, Integritätsfehler, Speichergrenzen, Rollback und Wiederherstellung relevant. Runtime- und Multiplayer-Proben folgen für jede unterstützte Version separat. [Toolchain-Recherche](../../docs/PS5_NATIVE_ALPHA.md)
