# Entwicklungsstand

Der lauffähige Host-Build verknüpft dynamische Kataloge, lokale Bibliothek, Paketinstallationen mit Prüfsummen und den isolierten HTML5-Player. GitHub wurde live abgerufen. Der itch.io-Abruf ist in der Cloud blockiert. Die Parser- und Paketläufe lassen sich mit `make test` wiederholen. In Chromium wurde Signal Drift über die Oberfläche installiert, im isolierten Player bis zum Sieg gespielt und deinstalliert.

PS5-Pakete, native Laufzeiten, DualSense auf der Konsole, externe Bibliothekskonten, Multiplayer und Recompilation haben keine Hardwarevalidierung und sind im aktuellen Build nicht verfügbar.

## Native Entwicklung

| Stufe | Ergebnis |
| --- | --- |
| Host-UI | SDL2-Build, Renderer, TSV-Parser, Navigation und simulierte Controller-Eingabe geprüft |
| PS5-Cross-Compilation | Beide C-Quellen mit den Zielheadern als x86-64-FreeBSD-ELF-Objekte kompiliert |
| PS5-Link und Paketierung | Autorisiertes SDK für Startcode, Link und signierte Paketierung steht aus |
| PS5-Installation, Start und Gameplay | Konsolenprüfung steht aus |
| Lokale Runtimes | Je zwei eigens kompilierte Testprogramme mit Java 17, 21 und 25 sowie mit Linux ELF ausgeführt |
| Katalog | GitHub-Releases und kuratierte ProsperoStore-Daten live geladen. itch.io in der Cloud blockiert |
| WSL | Build-Wrapper enthalten. Windows-Ausführung steht aus |

Die native Oberfläche liest einen vom Host erzeugten Snapshot. Die Paketverwaltung läuft im Host-Hub. Native Konsolen-Runtimes, Spielinstallation auf PS5 und Registrierung im Hauptmenü brauchen weitere Integrationsarbeit. [PS5-Build und Konsolentests](platform/ps5/README.md)

## Native Alpha 0.1.0

Die SDL2-Anwendung wurde für den Ziel-Sysroot kompiliert und zu einem ELF64-DYN-Modul gelinkt. Suche, Plattformfilter und Bibliothek sind in der SDL-UI verfügbar. Die Release-Pipeline validiert den Ordner eines autorisierten SDK-Builds und erzeugt ein reproduzierbares ZIP mit Dateihashes. Der verwendete Testordner ist eine Fixture. Ein echtes Akeno-`eboot.bin`, Konsolenstart, Gameplay und Multiplayer stehen aus. [Build und Hardwareplan](platform/ps5/README.md)
