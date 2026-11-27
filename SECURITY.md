# Sicherheit

## Native App

- **Kataloge.** ProsperoStore wird nur verwendet, wenn `manifest.sig` (Ed25519) mit einem der beiden
  veröffentlichten Schlüssel gültig ist, die Sequenz nicht kleiner als die zuletzt akzeptierte ist und
  jede verwendete API-Datei den SHA-256 aus dem Manifest hat. Der Offline-Cache wird bei jedem Start
  genauso geprüft.
- **Downloads.** Jeder Download wird beim Empfang gehasht und mit dem Katalog-SHA-256 (ProsperoStore)
  bzw. dem von GitHub veröffentlichten Asset-Digest (RecompStore, GitHub) verglichen. Ohne bekannte
  Prüfsumme wird nichts installiert.
- **Archive.** Genau ein Titelordner mit `eboot.bin` und `sce_sys/param.json`; abgelehnt werden
  absolute Pfade, `..`, Backslashes, Doppelpunkte, Links und Spezialdateien, verschlüsselte Einträge,
  unbekannte Kompression, Duplikate sowie Größen- und Anzahlüberschreitungen. Die Title-ID im Archiv
  muss zum Katalog passen.
- **Aktivierung.** Entpackt wird neben dem Ziel; erst ein Umbenennen aktiviert den neuen Ordner, die
  vorherige Version wird gesichert und bei einem Fehler zurückgelegt. Laufende Titel werden nicht
  verändert.
- **Netz.** Nur HTTPS mit Zertifikats- und Hostnamenprüfung, höchstens fünf Weiterleitungen, jede
  erneut geprüft; Antwortgrößen sind begrenzt.
- **Starts.** Die Start-Payload liest nur Anfragen aus `/data/akeno/launch` und den Kachelordnern und
  startet ausschließlich Title-IDs im Format `AAAA00000`.
- **Hinweis.** Der ProsperoStore-Release-Scan („bleibt in der Sandbox" / „verlässt die Sandbox") wird
  angezeigt; er ist ein Hinweis, keine Garantie.

## Python-Host-Hub

Bindet nur an `127.0.0.1`, prüft Host/Origin, spielt HTML5 in einer Sandbox mit strenger CSP und
installiert nur ZIPs aus Betreiber-Manifesten mit SHA-256. Details in den Quelltexten unter
`core/`, `security/` und `runtimes/`.

Sicherheitsprobleme bitte als privates GitHub-Advisory im Repository melden.
