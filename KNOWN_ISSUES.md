# Bekannte Einschränkungen

- **Konsolentest von `01.003.000` offen.** Akeno startet auf der PS5 und lädt die Kataloge; in
  `01.002.000` scheiterte jede Installation mit `sceHttp 0x80431073` (behoben, ungetestet).
  Installation, Starts, Verknüpfungen, Parallax-Startadresse und MihawkOS-Installationen sind auf der
  Konsole unbestätigt ([Testplan](INSTALLATION.md#erster-test-auf-der-ps5)).
- **Große Installationen sind langsam.** Die Konsole drosselt Schreibzugriffe eines Titels nach einigen
  hundert MB auf rund 2 MB/s. ProsperoStore lagert das Entpacken in einen Loader-Worker aus; Akeno
  entpackt noch im eigenen Prozess.
- **Deinstallation entfernt die Hauptmenü-Kachel nicht sofort.** ProsperoStore ruft dafür
  `sceAppInstUtilAppUnInstall` aus einem Loader-Worker auf; Akeno entfernt nur den Ordner. Die Kachel
  verschwindet, wenn ShadowMountPlus neu scannt.
- **Browserspiele:** Direktstart nur mit Parallax Browser; ProsperoWeb nimmt keine Start-URL an
  (Akeno zeigt dann Adresse und QR-Code). Ob ein Browserspiel im PS5-Browser flüssig läuft, ist je
  Spiel ungeprüft.
- **Windows-Spiele über MihawkOS** setzen voraus, dass MihawkOS einmal gestartet wurde (es legt dabei
  sein Laufwerk C: an). Installierbar sind nur Releases, für die GitHub einen SHA-256 veröffentlicht;
  ältere Release-Assets haben oft keinen. Ob ein Spiel unter MihawkOS läuft, ist ungetestet.
- **Akeno-Katalog:** Die Release-Dateinamen der Windows-Builds (Luanti, OpenRA, SRB2) stammen aus
  den Projekten bekannten Namensmustern; geprüft werden sie erst, wenn die Konsole das Release
  abfragt. Passt kein Asset, sagt Akeno das und installiert nichts.
- **Runtime-Argumente.** MihawkOS (`profile=`, `path=`) und Parallax Browser (Adresse) nehmen
  Startargumente; andere Runtimes werden ohne Datei geöffnet und zeigen ihre eigene Bibliothek.
- **GOG, Epic, itch.io-PC-Downloads** sind ohne Konto-Anbindung nur Entdecken-Einträge (GOG-Spiele
  lassen sich mit TrinityXSX auf der Konsole installieren).
- **Downloads** laufen nacheinander; Pausieren gibt es nicht (Abbrechen und erneut starten setzt nicht
  fort, nur ein unterbrochener Versuch innerhalb desselben Auftrags).
- **GitHub-API-Limit.** RecompStore- und GitHub-Installationen lösen Assets über die öffentliche
  GitHub-API auf (60 Anfragen pro Stunde und IP).
- **Datei-Images.** `.ffpkg`/`.ffpfsc`-Listungen werden angezeigt, aber nicht installiert.
- **Sprache.** Die Oberfläche ist Deutsch; Katalogtexte erscheinen in der Sprache der Entwickler.
- **Bildschirmtastatur** ist Akenos eigene; das System-IME wird nicht verwendet.
