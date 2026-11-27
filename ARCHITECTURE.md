# Architektur

1. **Stores** liefern normalisierte Metadaten. Öffentliche Feeds entdecken Titel. Ein explizit eingebundenes Manifest kann zusätzlich eine geprüfte Downloadquelle, Lizenz, Version, Runtime, Architektur, Startdatei und Prüfsumme angeben.
2. **Core** führt Quellen zusammen, behält bei Abruffehlern die zuletzt empfangenen Daten im laufenden Prozess und verarbeitet Installations- und Deinstallationsaufträge im Hintergrund.
3. **Installer** lädt Archive in separate Arbeitsverzeichnisse, verifiziert SHA-256, prüft ZIP-Einträge und veröffentlicht eine aktive Generation atomar. Bibliothek und Pakete liegen außerhalb des Checkout.
4. **Runtime** stellt installierte HTML5-Dateien in einem Browser-Frame mit eigener Schutzrichtlinie bereit. Die Server-API hält die Benutzeroberfläche und die Ausführung getrennt.
5. **Kompatibilität** erfasst Installation und Player-Auslieferung mit Spiel- und Runtime-Version in SQLite. Heuristiken dienen als Engine-Hinweise. Gameplay bleibt unbestätigt.
6. **App** führt die Befehle über die lokale HTTP-Schnittstelle aus. Die Navigation unterstützt Tastatur und Gamepad-Ereignisse. Die Runtime leitet Richtungsimpulse an Spiele weiter, die das Nachrichtenformat unterstützen.

Die Linux-Implementierung nutzt Python, SQLite und das Dateisystem. Der native Zielpfad wird in [platform/ps5](platform/ps5/README.md) beschrieben.

Der native SDL2-Katalog bindet die Daten per TSV an. `core.native_catalog` schreibt einen Snapshot aus den gemeinsamen Providern und dem installierten Bibliotheksstand. Das native Programm rendert und navigiert beide Ansichten. Auf dem Linux-Host gibt es zusätzlich einen bewusst lokalen Prozess-Supervisor für Java und ELF. Die Signaturprüfung kann den Host-Runner für eine ausdrücklich ausgewählte Datei auflösen. Dieser Pfad wird nicht von entfernten Katalogdaten ausgelöst.
