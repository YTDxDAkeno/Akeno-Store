# Bekannte Einschränkungen

- **Konsolentest offen.** Der erste Start endete mit CE-107750-0 (fehlerhaftes RELRO-Segment, im
  Build behoben); der Start mit dem korrigierten Paket ist noch nicht bestätigt. Leistung des SDL-Software-Renderers bei 1080p, `sceHttp`, Installation, Starts und
  Verknüpfungen sind auf der Konsole unbestätigt ([Testplan](INSTALLATION.md#erster-test-auf-der-ps5)).
- **Große Installationen sind langsam.** Die Konsole drosselt Schreibzugriffe eines Titels nach einigen
  hundert MB auf rund 2 MB/s. ProsperoStore lagert das Entpacken in einen Loader-Worker aus; Akeno
  entpackt noch im eigenen Prozess.
- **Deinstallation entfernt die Hauptmenü-Kachel nicht sofort.** ProsperoStore ruft dafür
  `sceAppInstUtilAppUnInstall` aus einem Loader-Worker auf; Akeno entfernt nur den Ordner. Die Kachel
  verschwindet, wenn ShadowMountPlus neu scannt.
- **Web-Runtime ohne Direktstart.** ProsperoWeb nimmt keine Start-URL an; Akeno zeigt die Adresse und
  einen QR-Code.
- **Runtime-Argumente.** Nur MihawkOS dokumentiert `path=`; andere Runtimes werden ohne Datei geöffnet
  und zeigen ihre eigene Bibliothek.
- **GOG, Epic, itch.io-PC-Downloads** sind ohne Konto-Anbindung nur Entdecken-Einträge.
- **GitHub-API-Limit.** RecompStore- und GitHub-Installationen lösen Assets über die öffentliche
  GitHub-API auf (60 Anfragen pro Stunde und IP).
- **Datei-Images.** `.ffpkg`/`.ffpfsc`-Listungen werden angezeigt, aber nicht installiert.
- **Sprache.** Die Oberfläche ist Deutsch; Katalogtexte erscheinen in der Sprache der Entwickler.
- **Bildschirmtastatur** ist Akenos eigene; das System-IME wird nicht verwendet.
