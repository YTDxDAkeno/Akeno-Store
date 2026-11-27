# Roadmap

1. **Konsolentest von `01.003.000`** nach [INSTALLATION.md](INSTALLATION.md#erster-test-auf-der-ps5):
   Installation, Start über Loader, Verknüpfung, Parallax-Start eines Browserspiels, eine
   MihawkOS-Installation. Ergebnisse mit Firmware in STATUS.md und als Testberichte im Akeno-Katalog.
2. **itch.io-Konto** über den API-Schlüssel des Nutzers: Bibliothek, gekaufte und kostenlose
   PC-Downloads (Windows → MihawkOS), Updates.
3. **Leistung auf der Konsole messen** (Frame-Zeiten); bei Bedarf Hintergrund und Glow weiter vorbacken
   oder auf ps5-opengl umstellen.
4. **Datei-Worker über den ELF-Loader** für schnelles Entpacken und `sceAppInstUtilAppUnInstall`
   (Vorbild ProsperoStore `helper/`).
5. **Warteschlangen-Steuerung:** Pause, parallele Downloads, Reihenfolge ändern.
6. **Runtime-Argumente** mit den Runtime-Projekten abstimmen (RetroArch-Kern + ROM), danach
   Verknüpfungen für einzelne Spiele in Runtimes (MihawkOS-Profile und Browserspiele zuerst).
7. **Freiwilliger Abgleich der Kompatibilitätsberichte** (Export → Pull Request in den Akeno-Katalog).
8. **Konten** nur über offiziell dokumentierte Wege: itch.io-API-Schlüssel des Nutzers, danach
   GOG/Epic, falls deren Bedingungen eine Drittanbieter-Anbindung erlauben.
9. **Sprachen** (Englisch) und Barrierefreiheit (größere Schrift, reduzierte Bewegung ausbauen).
