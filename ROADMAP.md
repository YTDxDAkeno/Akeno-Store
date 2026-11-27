# Roadmap

1. **Konsolentest** nach [INSTALLATION.md](INSTALLATION.md#erster-test-auf-der-ps5): Start, Eingabe,
   `sceHttp`, Installation, Start über Loader, Verknüpfung. Ergebnisse mit Firmware in STATUS.md.
2. **Leistung auf der Konsole messen** (Frame-Zeiten); bei Bedarf Hintergrund und Glow weiter vorbacken
   oder auf ps5-opengl umstellen.
3. **Datei-Worker über den ELF-Loader** für schnelles Entpacken und `sceAppInstUtilAppUnInstall`
   (Vorbild ProsperoStore `helper/`).
4. **Weitersetzbare Downloads** (HTTP-Range) und Warteschlangen-Steuerung.
5. **Runtime-Argumente** mit den Runtime-Projekten abstimmen (RetroArch-Kern + ROM, ProsperoWeb-URL),
   danach Verknüpfungen für einzelne Spiele in Runtimes.
6. **Konten** nur über offiziell dokumentierte Wege: itch.io-API-Schlüssel des Nutzers, danach
   GOG/Epic, falls deren Bedingungen eine Drittanbieter-Anbindung erlauben.
7. **Sprachen** (Englisch) und Barrierefreiheit (größere Schrift, reduzierte Bewegung ausbauen).
