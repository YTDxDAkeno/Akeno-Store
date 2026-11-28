# Bekannte Einschränkungen

- **Konsolentest von `01.004.000` offen.** Akeno startet auf der PS5 und lädt die Kataloge. Downloads,
  der Spielmodus (Neustart mit `--play`), die eingebaute RPG-Maker-Runtime und die Kacheln pro Spiel
  sind auf der Konsole unbestätigt ([Testplan](INSTALLATION.md#erster-test-auf-der-ps5)).
- **Nur eine eingebaute Runtime.** Gespielt werden können native PS5-Titel und RPG-Maker-2000/2003-Spiele.
  Windows, Linux, Java, HTML5, Godot, Unity, Ren'Py, .NET und ROMs sind geplant
  ([docs/RUNTIME_RESEARCH.md](docs/RUNTIME_RESEARCH.md)) und werden bis dahin als „Nicht kompatibel"
  angezeigt.
- **RPG-Maker-Runtime:** Musik in MP3 und OGG wird noch nicht abgespielt (WAV und MIDI schon); Texte
  werden nur als Windows-1252 gelesen (japanische Originale zeigen falsche Zeichen); RTP-abhängige Spiele
  brauchen eine eigene RTP-Kopie in `/data/akeno/rtp`. Der Software-Renderer skaliert das
  320×240-Bild hoch.
- **itch.io:** Downloads brauchen den eigenen API-Schlüssel; mit einem echten Schlüssel ist der Ablauf
  noch nicht getestet. itch.io veröffentlicht keine SHA-256-Werte; geprüft wird die von itch.io genannte
  Größe.
- **Große Installationen sind langsam.** Die Konsole drosselt Schreibzugriffe eines Titels nach einigen
  hundert MB auf rund 2 MB/s. ProsperoStore lagert das Entpacken in einen Loader-Worker aus; Akeno
  entpackt noch im eigenen Prozess.
- **Deinstallation entfernt die Hauptmenü-Kachel nicht sofort.** ProsperoStore ruft dafür
  `sceAppInstUtilAppUnInstall` aus einem Loader-Worker auf; Akeno entfernt nur den Ordner. Die Kachel
  verschwindet, wenn ShadowMountPlus neu scannt.

- **Start aus einer Kachel, während Akeno läuft:** Das System holt dann vermutlich nur Akeno in den
  Vordergrund, statt das Spiel zu starten (nicht geprüft).
- **GOG, Epic** sind Entdecken-Einträge: ihre Spiele brauchen Akenos Windows-Runtime und ein Konto.
- **Downloads** laufen nacheinander; Pausieren gibt es nicht (Abbrechen und erneut starten setzt nicht
  fort, nur ein unterbrochener Versuch innerhalb desselben Auftrags).
- **GitHub-API-Limit.** RecompStore- und GitHub-Installationen lösen Assets über die öffentliche
  GitHub-API auf (60 Anfragen pro Stunde und IP).
- **Datei-Images.** `.ffpkg`/`.ffpfsc`-Listungen werden angezeigt, aber nicht installiert.
- **Sprache.** Die Oberfläche ist Deutsch; Katalogtexte erscheinen in der Sprache der Entwickler.
- **Bildschirmtastatur** ist Akenos eigene; das System-IME wird nicht verwendet.
