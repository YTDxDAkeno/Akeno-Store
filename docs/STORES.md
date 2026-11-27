# Store-Anbindungen

Akeno zeigt veröffentlichte Projekte und Releases im Katalog an. Für eine Installation benötigt der Hub ein autorisiertes, unterstütztes Paket mit passender Prüfsumme. Ein Katalogeintrag bestätigt weder die Installierbarkeit noch den Start auf einer PS5.

| Quelle | Daten | Stand |
| --- | --- | --- |
| GitHub | Öffentliche Atom-Feeds mit Release-Tag, Release-Titel und Zeitstempel. Für ausgewählte Projekte ergänzt Akeno Beschreibung, Genre, Zielplattform und den Geltungsbereich der Codelizenz. | Live geprüft für Mindustry, EasyRPG Player, SuperTuxKart, OpenGOAL, Ship of Harkinian, ProsperoCraft, PokeMMO Prospero und Dusklight. |
| ProsperoStore | Offizielle Quellrecords für zehn kuratierte Spiele, einschließlich SudokuZen, Lantern Miner, Dungeon Crawl Stone Soup, NetHack, Brogue CE, ProsperoPuzzles und OpenRCT2. Ausgewählte Einträge enthalten Cover aus dem Original-Repository. | Alle zehn Quellrecords live geprüft. Der Katalog liefert Metadaten, keine Installationspakete an Akeno. |
| itch.io | Der öffentliche RSS-Feed für kostenlos gelistete Spiele liefert Titel, Entwickler, Kategorien und Veröffentlichungsdaten, soweit der Feed sie meldet. Externe Vorschauen, Feed-HTML und als eingeschränkt markierte Einträge werden ausgeblendet. | Import mit RSS-Testdaten geprüft. Der Live-Test erreicht in der Cloud den Proxy und erhält HTTP 403. |
| Eigene Manifeste | Versionierte JSON-Kataloge mit autorisierten HTML5-ZIP-Paketen, Lizenzangaben, SHA-256, Startdatei und optionaler Beschreibung. Betreibergeprüfte Covers und bis zu sechs Screenshots können bei Einstufung `all` oder `teen` als HTTPS-Metadaten veröffentlicht werden. | Lokale Validierung und Import implementiert. Medienmetadaten allein prüfen keine Bildrechte und laden die Bilder nicht herunter. |
| GOG, Game Jolt und RecompStore | Eine nachgewiesene öffentliche Katalogschnittstelle oder Anbieterautorisierung für Akeno steht aus. | In dieser Version gibt es für diese Dienste keine Kontosynchronisierung, Downloadintegration oder nachgebildeten Ergebnisse. |

Links zu den jeweils öffentlichen Verträgen und Quellen finden sich am Ende. Der ProsperoStore-Upstream erzeugt seine öffentliche API aus den Quellrecords. Ein späterer Installer braucht einen freigegebenen nativen Paketpfad sowie die nachweisliche Signatur-, Artefakt- und Rollback-Prüfung des Veröffentlichungskanals. Die öffentlichen GitHub- und itch.io-Feeds enthalten keine Besitznachweise für Käufe.

## Live-Prüfung unter Linux, WSL oder PowerShell

Im Projektordner mit Python 3.12 testen. Die normalen Tests ohne `AKENO_LIVE_STORES` arbeiten ohne externe Dienste.

```bash
AKENO_LIVE_STORES=github,prosperostore python3 -m unittest discover -s tests -p test_stores_live.py -v
AKENO_LIVE_STORES=itch python3 -m unittest discover -s tests -p test_stores_live.py -v
```

```powershell
$env:AKENO_LIVE_STORES = 'itch'
py -3.12 -m unittest discover -s tests -p test_stores_live.py -v
Remove-Item Env:AKENO_LIVE_STORES
```

Der itch.io-Test lädt den echten öffentlichen Feed und verlangt mindestens einen sichtbaren Eintrag. Ein Verbindungsfehler führt zu einem fehlgeschlagenen Test. Für den öffentlichen Feed sind weder Anmeldung noch Zugriffstoken nötig. Für die Cloud-Freigabe genügt die Domain `itch.io` für diesen Test.

## Eigenen Katalog prüfen

```bash
python3 -m stores manifests/example.catalog.json
python3 -m core.server --offline --manifest /pfad/zum/katalog.json
```

Die Option `--catalog-url` akzeptiert HTTPS-Quellen, die der Betreiber ausdrücklich ausgewählt hat. Die Beispiel-Datei ist leer. Die Felder für eigene autorisierte, geprüfte Pakete stehen im [Manifest-Schema](../manifests/catalog.schema.json).

- [GitHub Releases](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases)
- [ProsperoStore API-Vertrag](https://github.com/darkxex/ps5-homebrew-catalog/blob/main/docs/api.md)
- [itch.io API-Dokumentation](https://itch.io/docs/api/overview)
- [itch.io öffentlicher kostenloser Feed](https://itch.io/games/price-free.xml)
