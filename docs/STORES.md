# Store-Anbindungen

Alle Endpunkte wurden am 27. November 2026 live abgefragt (`make -C native probe`).

| Store | Endpunkt | Vertrauen | Akeno-Funktion | Live |
| --- | --- | --- | --- | --- |
| ProsperoStore | `https://prosperostore.net/api/v1/` (`manifest.json`, `manifest.sig`, `index.json`, `apps/<id>.json`) | Ed25519-Signatur (Schlüssel `da351006acb6e3c3`, `eef399ea3007720a`), Sequenz nie rückwärts, SHA-256 jeder Datei, SHA-256 jedes Downloads | Installieren, Aktualisieren, Entfernen; Detailseite lädt den geprüften App-Datensatz | 243 Einträge |
| RecompStore | `https://www.recompstore.com/catalog.json` (`v2Apps`, `apps`, `comingSoon*`) | Katalog unsigniert, meist ohne SHA-256 → Download nur, wenn GitHub einen SHA-256-Digest für das Asset veröffentlicht (und er mit einem Katalogwert übereinstimmt) | Installieren über das neueste GitHub-Release; „Benötigt" zeigt das eigene Original | 162 Einträge |
| itch.io | `https://itch.io/games/free/platform-web.xml`, `https://itch.io/games/free.xml` | öffentliche Feeds, Erwachsenen-Filter | Browserspiele → Web-Runtime; übrige → Store-Seite | 60–61 |
| Game Jolt | `https://gamejolt.com/site-api/web/discover/games?section=hot` | öffentlich, `has_adult_content` gefiltert | Browserspiele → Web-Runtime; übrige → Store-Seite | 36 |
| GOG | `https://catalog.gog.com/v1/catalog?price=between:0,0…` | öffentlich | Entdecken kostenloser Spiele, Windows-Runtime-Hinweis | 48 |
| Epic Games | `https://store-site-backend-static.ak.epicgames.com/freeGamesPromotions` | öffentlich | Aktuelle Gratis-Aktionen (Prozentsatz 0, laufender Zeitraum) | 2 |
| GitHub | `https://api.github.com/repos/<owner>/<repo>/releases/latest` | Asset-Digest von GitHub | Eigene Repositories (Einstellungen) | — |

## Was bewusst nicht gemacht wird

- **Keine Downloads ohne Prüfsumme.** Ein RecompStore-Eintrag, dessen GitHub-Asset keinen Digest hat,
  wird nicht installiert.
- **Keine Konto-Integrationen ohne offizielle Autorisierung.** GOG- und Epic-Bibliotheken setzen deren
  OAuth-Clients voraus; Akeno zeigt dort nur, was öffentlich ist, und verlinkt die Store-Seite (QR).
  Gekaufte Windows-Spiele können nach dem Download am PC als eigene Dateien in `/data/akeno/games`
  gelegt und über die Windows-Runtime gestartet werden.
- **itch.io-Downloads** für PC brauchen die itch.io-API mit Schlüssel des Nutzers; das ist noch nicht
  eingebaut. Browserspiele funktionieren ohne Konto.
- **Kein Umgehen von Kaufprüfungen oder Serverautorisierungen.**

## Neuen Provider hinzufügen

`native/src/stores/provider.hpp`: `refresh()` liefert `Game`-Einträge mit ehrlicher `Access`-Art;
`resolve()` füllt `artifact_url` und `sha256` erst bei Bedarf. Parser gehören in den Namensraum
`parse` und bekommen Tests in `native/tests/core_tests.cpp`.
