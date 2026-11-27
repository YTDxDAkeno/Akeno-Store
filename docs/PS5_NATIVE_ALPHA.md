# Native Alpha – Referenzen und Buildnachweis

Die Prüfung korrigiert die frühere Annahme zum proprietären SDK. PS5 Native App Boilerplate dokumentiert einen öffentlichen Build mit LLVM und eigenen C/C++-Werkzeugen, der ohne proprietäre Sony-SDK-Dateien auskommt. Das Projekt erzeugt ein Entwicklungs-FSELF und verwendet eine modifizierte Ladeumgebung für den Start. ProsperoStore baut auf dieser Grundlage auf. Für Akeno wurden der spezifizierte Anwendungsordner und die Trennung von Anwendung, Metadaten, Runtime und Daten übernommen. Der geprüfte Akeno-Build endet beim normalen Linken der SDL2-Anwendung.

| Projekt und geprüfter Commit | Lizenzstand und Befund |
| --- | --- |
| [PS5 Native App Boilerplate](https://github.com/drakmor/ps5-native-app-boilerplate/tree/470695e0c557) | GPL-3.0-or-later. C/C++-Build mit Clang und LLD 18, gepinntem öffentlichem SDK und optionalen Paketwerkzeugen. CI prüft das Tooling und die Runtime. Der Ordner enthält `eboot.bin`, `sce_sys`, Laufzeitdateien und `assets`. Upstream meldet Tests auf 6.02 und 12.70. |
| [ps5-app-sdk](https://github.com/cybervisi0n/ps5-app-sdk/tree/35d2190c62ae) | GPL-3.0. CI-Build und Homebrew-Dokumentation. Die Startanleitung setzt eine modifizierte Ladeumgebung voraus. Eine unabhängige, autorisierte Start- und Signierumgebung für Akeno ließ sich daraus nicht ableiten. |
| [ps5link-sdk](https://github.com/Rufidj/ps5link-sdk/tree/ea771e535378) | GPL-3.0. Öffentliche C-Schnittstellen und Toolchain-Verweise. Der Upstream-Bericht zu 9.00 gilt für seine eigenen Proben. |
| [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk/tree/4122b714c7e7) | GPL-3.0 mit dateispezifischen Lizenzen. Linux- und macOS-CI. Der geprüfte PacBrew-Sysroot enthält die Zielheader, statische POSIX-Bibliotheken und Linkerimporte. |
| [ProsperoStore](https://github.com/darkxex/ProsperoStore/tree/0e1f03188964) | GPL-3.0. Build und Runtime folgen dem Boilerplate. Das Projekt nutzt eine UI-Bibliothek und ein PS5-OpenGL-Backend. Upstream dokumentiert Installations- und Updatetests für 6.02 und 12.70. |
| [ProsperoWeb](https://github.com/kvaps/ProsperoWeb/tree/837753120e8f) | GPL-3.0 für den Repository-Hauptcode. WebKit und Medienbibliotheken haben eigene Lizenzen. CMake, Ninja und gepinnter WebKit-Port mit getrennten Host- und Konsolen-Builds in der CI. Upstream meldet Hardwaretests auf 13.42. |
| [Prospero Win](https://github.com/mpereiraesaa/prospero-win/tree/18115ce13e07) | LGPL-2.1-or-later für den Hauptcode. Komponentenweise Lizenzierung für Wine, Grafiktreiber und Werkzeuge. Linux-Build und CI vorhanden. Der veröffentlichte Konsolenworkflow hängt von modifizierter Ladeumgebung und optionalen Helfern ab. |

## ProsperoStore als Referenz

Der Store synchronisiert Katalogdokumente über HTTPS. Er prüft Ed25519-Signatur, SHA-256 und eine Sequenznummer vor Installationsentscheidungen. Hintergrunddienste behandeln Katalog und Bilder getrennt. Archivgröße, Hash und Verzeichnisstruktur werden vor Installation geprüft. Fehler gelangen in Diagnose und Belege. Der Updater arbeitet mit dem vorherigen Installationsstand und legt Dateivorgänge außerhalb der UI ab. Konsoleninstallation und Hauptmenü-Anzeige hängen von den Systemdiensten und der in der Projektanleitung genannten Umgebung ab. Der Store startet laut README selbst keine installierten Spiele. [Implementierungsnotizen](https://github.com/darkxex/ProsperoStore/blob/0e1f03188964/IMPLEMENTATION_NOTES.md)

Akeno verwendet die kuratierten öffentlichen Quellrecords für die Anzeige. Es prüft Title-ID und Repository-Identität, lädt bekannte Cover aus dem jeweiligen Original-Repository und gibt auf dem Host die Anzeige frei. Vor einer Konsoleninstallation wären die signierte API, Artefaktverifikation und ein dokumentierter Installationsvertrag erforderlich. Die hostseitige ZIP-Prüfung und atomare Aktivierung sind bereits in Akenos Installer implementiert. [Katalogvertrag](https://github.com/darkxex/ps5-homebrew-catalog/blob/main/docs/api.md)

## Linkprüfung und konkrete Fehler

Der erste Aufruf von LLD mit `-shared --no-undefined`, SDL2, den SDL2-Abhängigkeiten und den SDK-Systemimporten meldete undefinierte Symbole `nl_langinfo`, `___mb_cur_max`, `strcasestr` und `tmpfile`. Diese POSIX-Funktionen liegen im statischen `libc.a` des gepinnten Sysroots. Der reproduzierbare Aufruf `bash build/build-ps5.sh module` bindet es am Ende der Bibliotheksliste ein. `readelf` weist ein ELF64-FreeBSD-DYN-Modul für x86-64 aus. Kein SDK-Startobjekt ist eingebunden.

Der Aufruf `bash build/build-ps5.sh sdk /pfad/zur/sdk.mk` erwartet die Compiler- und SDL2-Linkflags eines autorisierten SDKs. Das vorhandene öffentliche Paketverfahren baut einen Entwicklungs-FSELF für eine modifizierte Ladeumgebung. Ein autorisiertes Akeno-`eboot.bin` und ein vollständiger Runtime-Satz stehen hier deshalb aus. Die ZIP-Pipeline wurde mit Testdaten geprüft und verwirft generische ELF-Dateien als `eboot.bin`. [Build und Abnahme](../platform/ps5/README.md)

## Runtime-Stand

SDL2 2.30.12 ist im Zielmodul statisch gelinkt. Der Host-Runner für Java 17, 21 und 25 sowie Linux ELF bleibt verfügbar. ProsperoWeb, Prospero Win und die dokumentierten Java-Ports sind Kandidaten für getrennte Anwendungen oder SDK-Integrationen. Dafür fehlen überprüfte Schnittstellen für Start, Stopp, Fehlerisolation und Updates innerhalb des Hubs. SDL3, Linux-ABI, EasyRPG, mkxp-z, Ren’Py und Godot bleiben auf der Konsole ungeprüft. Format-Erkennung ist im Host verfügbar und aktiviert keine Runtime allein aufgrund einer Dateiendung. [Host-Runtimes](RUNTIMES.md)
