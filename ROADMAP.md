# Roadmap

Grundsatz: Spiele laufen in Akeno selbst. Jede neue Engine kommt als eingebaute Runtime in Akenos
Spielmodus, nicht als Weiterleitung an eine andere App. Grundlagen: [docs/RUNTIME_RESEARCH.md](docs/RUNTIME_RESEARCH.md).

1. **Konsolentest von `01.004.000`** nach [INSTALLATION.md](INSTALLATION.md#erster-test-auf-der-ps5):
   Downloads, Spielmodus (Neustart mit `--play`), RPG-Maker-Runtime, Kachel pro Spiel, itch.io-Schlüssel.
   Ergebnisse mit Firmware in STATUS.md.
2. **RPG-Maker-Runtime vervollständigen:** OGG- und MP3-Musik (libvorbis/Tremor, mpg123 oder minimp3),
   Textkodierungen außer Windows-1252 (Shift-JIS usw.), Vollbild-Skalierung prüfen.
3. **Windows-Runtime** nach dem Aufbau von Prospero Win: Wine-PRX-Module als interne Akeno-Abhängigkeit
   laden (aus `/data/akeno/runtimes`), Senken für VideoOut, DualSense und Audio im Spielmodus, Profile pro
   Spiel. Danach GOG- und itch.io-Windows-Spiele.
4. **Linux-Runtime** nach dem PokeMMO-Prospero-Loader (Linux-ABI, SDL2/SDL3, OpenGL über ps5-opengl);
   Godot- und Unity-Linux-Exporte als erste Ziele.
5. **Java-Runtime** auf Basis des OpenJDK-25-Ports (Mindustry, libGDX).
6. **HTML5-Runtime** (WebKit ohne Browser-Oberfläche) für heruntergeladene Web-Spiele und RPG Maker MV/MZ.
7. **ROM-Runtime** mit libretro-Kernen im Spielmodus für eigene Dumps.
8. **Download-Manager:** Pause, parallele Downloads, Reihenfolge ändern.
9. **Freiwilliger Abgleich der Kompatibilitätsberichte** (Export → Pull Request in den Akeno-Katalog).
10. **Sprachen** (Englisch) und Barrierefreiheit.
