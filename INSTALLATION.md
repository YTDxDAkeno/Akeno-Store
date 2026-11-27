# Installation auf dem Entwicklungsrechner

Starte im entpackten Build oder im vorhandenen Checkout:

```sh
python3 -m core.server --offline --port 8765 --data-dir /tmp/akeno-hub
```

Öffne die vom Programm ausgegebene Adresse im Browser. Der Server lauscht ausschließlich auf der lokalen Schnittstelle. Öffne Signal Drift, installiere es und wechsle mit **Spielen** in den Player. Verbinde drei Relais und erreiche das markierte Zielfeld. Verwende Pfeiltasten oder die Bildschirmsteuerung. Im Hub kannst du das Spiel erneut installieren oder entfernen.

Ohne `--data-dir` verwendet Akeno `XDG_DATA_HOME/akeno-hub` oder `~/.local/share/akeno-hub`. Verwende einen dauerhaften Pfad, wenn die Bibliothek und Kompatibilitätsdaten über Neustarts erhalten bleiben sollen.

Aktiviere öffentliche Quellen, indem du `--offline` weglässt. `--manifest PFAD` und `--catalog-url HTTPS-URL` binden geprüfte Betreiberkataloge ein. Anschließend aktualisierst du die Quellen im Hub. Nur ein vollständiger Eintrag aus einem vertrauenswürdigen Manifest kann installiert werden.

## Native Oberfläche

Baue die Anwendung mit `make native` und exportiere die Daten mit `make native-catalog`. Starte anschließend `dist/native/akeno-hub dist/native/data/catalog.tsv dist/native/data/library.tsv`. Der native Build verwendet einen Katalog-Snapshot und zeigt bereits installierte Spiele an. Die Paketverwaltung läuft derzeit im Entwicklungs-Hub. Konfiguration für WSL und Ziel-SDK steht in der [Build-Anleitung](BUILD.md).
