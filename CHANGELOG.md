# Änderungen

## 0.1.0 – Host-Entwicklungsbuild

- Lokaler Hub mit Katalog, Bibliothek, Installationsaufträgen, Runtime- und Diagnoseinformationen.
- GitHub-Releases über Atom, itch.io über öffentliches RSS und versionierte JSON-Kataloge.
- Geprüfte ZIP-Installationen und Reparatur über atomaren Versionswechsel.
- HTML5-Player, das Offline-Puzzle Signal Drift und eine lokale Kompatibilitätsdatenbank.
- Reproduzierbarer Host-Build und automatisierte Tests.

## Native SDL2 und Laufzeittests

- Native C-Oberfläche für Katalog und Bibliothek mit Tastatur- und Controllernavigation.
- TSV-Export aus der vorhandenen Bibliothek und den gemeinsamen Store-Providern.
- Cross-Compilation der SDL-Oberfläche zu ELF-Zielobjekten und Build-Wrapper für WSL.
- Live-Metadaten aus GitHub und dem kuratierten ProsperoStore-Katalog.
- Signaturbasierte Dateierkennung und beobachtete Runtime-Auswahl für vertrauenswürdige lokale Java- und Linux-Programme.
- Host-Prozessüberwachung mit Zeitlimit, kontrolliertem Stopp, Fehlerstatus, Ausgabe und Safe Mode.
