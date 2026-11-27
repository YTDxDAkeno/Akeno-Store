# Abdeckung des Master-Prompts

Stand 27. November 2026, Akeno `01.003.000`. Diese Liste beantwortet „wo ist was" ehrlich: **umgesetzt**
heißt im Code vorhanden und auf dem Linux-Host getestet; auf der PS5 belegt ist nur, was
[STATUS.md](../STATUS.md) so ausweist. **Teilweise** und **offen** nennen den Grund.

## Architektur (Abschnitt 4)

| Baustein | Stand | Wo |
| --- | --- | --- |
| Akeno Core | umgesetzt | `native/src/core`, `native/src/platform` |
| Store Engine | umgesetzt (8 Provider) | `native/src/stores` |
| Runtime Manager | umgesetzt als Routing zu vorhandenen PS5-Runtime-Titeln (eigene Wine-/Java-/Browser-Ports baut Akeno bewusst nicht) | `native/src/runtime/runtimes.cpp`, Tab „Runtimes" |
| Compatibility Engine | umgesetzt: Katalogstatus + eigene Testberichte | `native/src/install/compat.cpp`, Detailseite, Filter „Getestet" |
| Game Manager | umgesetzt: Installieren, Aktualisieren, Reparieren (= neu installieren), Entfernen | `native/src/install` |
| Recomp Engine | teilweise: RecompStore-Recomps installieren, eigene Dumps erkennen und der passenden Runtime zuordnen. Eine eigene Recompilation-Pipeline auf der Konsole gibt es nicht. | `stores/recompstore.cpp`, `runtime/runtimes.cpp` |
| Akeno Online | teilweise: Online-, Crossplay- und Server-Angaben je Spiel. Serverbrowser, Freunde und Einladungen sind Sache der Spiele selbst. | Akeno-Katalog, Detailseite |
| Shortcut Manager | umgesetzt (auf der PS5 unbestätigt) | `runtime/launch.cpp` |
| Diagnostics | umgesetzt: Tab „Diagnose" mit Netzwerktest und Bericht `/data/akeno/diagnose.txt` | `ui/app.cpp` |

## Stores (Abschnitte 5 und 6)

| Store | Stand |
| --- | --- |
| ProsperoStore | umgesetzt, signiert (Ed25519), installierbar |
| RecompStore | umgesetzt, installierbar mit GitHub-SHA-256 |
| GitHub | umgesetzt: eigene Repositories (Einstellungen) und Windows-Builds aus dem Akeno-Katalog |
| Akeno-Katalog | **neu**: 38 kuratierte Spiele aus Abschnitt 9 mit ehrlichem Weg (nativ / Browser / MihawkOS / extern) |
| itch.io | teilweise: kostenlose Spiele und Browserspiele aus den öffentlichen Feeds. **Offen:** Konto, Bibliothek und PC-Downloads über die itch.io-API mit dem Schlüssel des Nutzers |
| Game Jolt | teilweise: Entdecken, Browserspiele |
| GOG | teilweise: kostenlose Spiele entdecken; Installieren über TrinityXSX (eigener GOG-Installer auf der Konsole) oder eigene Dateien |
| Epic Games | teilweise: aktuelle Gratis-Aktionen entdecken; kein Konto (keine offizielle Drittanbieter-Anbindung) |
| Steam | offen: nur über einen Steam-Client in MihawkOS denkbar; nicht getestet |
| Amazon, Humble | offen: keine öffentliche Schnittstelle ohne Konto-Autorisierung |
| Flathub, Lutris | offen: liefern Linux-Pakete bzw. Linux-Installationsskripte; eine allgemeine Linux-Runtime gibt es auf der PS5 nicht |
| Mod DB | offen |

## Runtimes (Abschnitte 7 und 8)

| Runtime | Stand |
| --- | --- |
| HTML5/WebGL | umgesetzt: **Parallax Browser** (`PPSA99207`) öffnet die Spieladresse direkt per Startargument; ProsperoWeb als Alternative mit QR-Code |
| Windows | umgesetzt: MihawkOS, PC5, Prospero Win, TrinityXSX; **neu:** Akeno installiert freie Windows-Spiele direkt in MihawkOS' `C:\Games` und startet sie mit `profile=<id>` |
| Java | teilweise: Minecraft über ProsperoCraft, J2ME über Capy Mobile. Eine allgemeine Java-Desktop-Runtime gibt es im signierten Katalog nicht |
| Linux-ELF | offen (keine allgemeine Linux-Runtime auf der PS5) |
| RPG Maker | teilweise: MV/MZ als Web-Export im Browser, XP/VX über MihawkOS (nicht getestet) |
| Ren'Py, Godot, Unity | teilweise: Web-Exporte im Browser, Windows-Builds über MihawkOS (nicht getestet) |
| Emulatoren (NES bis Switch) | umgesetzt als Routing zu RetroArch, PS5SX2, Porpoise usw. |
| Engine-Erkennung | umgesetzt: Dateiköpfe (ROM-Header, PE, ELF, Godot-PCK, Ren'Py, RPG Maker, Unity WebGL …), ohne etwas auszuführen |
| Automatische Runtime-Installation | umgesetzt: fehlt die Runtime, bietet Akeno sie zur Installation an |

## Spiele (Abschnitt 9)

Alle genannten Spiele stehen im Akeno-Katalog (`native/assets/catalog/akeno.json`), jedes mit dem
tatsächlichen Weg auf die PS5:

- **Nativ im signierten ProsperoStore:** PokeMMO, Minecraft (ProsperoCraft), SuperTuxKart, osu!,
  dazu Singleplayer-Spiele wie Dungeon Crawl Stone Soup, NetHack, Brogue, Celeste Classic, DOOM.
- **Im Browser:** Pokémon Showdown, PokéRogue, TETR.IO, Stendhal.
- **Windows über MihawkOS (geprüfter GitHub-Download):** Luanti, OpenRA, Sonic Robo Blast 2.
- **Mit Begründung, warum noch nicht direkt:** Ring Racers, Warzone 2100, Xonotic, OpenArena,
  Warfork, AssaultCube, DDNet, Mindustry, Beyond All Reason, Veloren, Ryzom, beide Toontown-Projekte,
  The Mana World, Pokémon Revolution/Blaze Online, Infinite Fusion, Insurgence, Reborn, Rejuvenation,
  Xenoverse, Uranium.

Kein Eintrag ist „Verifiziert spielbar": Das setzt einen Test auf einer PS5 voraus.

## Weitere Abschnitte

| Abschnitt | Stand |
| --- | --- |
| 10 Altersfilter | teilweise: Inhalte ab 18 aus den Stores ausgeblendet, solange die Einstellung aus ist. Offen: Profile, regionale Regeln |
| 11 Online/Crossplay | teilweise: Angaben je Spiel; die Verbindung machen die Spiele selbst |
| 12 Recompilation/ROM-Import | teilweise: Import-Ordner `/data/akeno/games` mit Formaterkennung; Recomps aus RecompStore. Keine eigene Recompilation |
| 13 Hauptmenü | umgesetzt (Konsolentest offen) |
| 14 Oberfläche | umgesetzt: Entdecken, Alles, Kostenlos, Online & Multiplayer, Singleplayer, Fanprojekte, Indie, PS5-Titel, Recomps, Emulatoren, Browser, Windows, Visual Novels, Getestet, Store-Provider, Bibliothek, Downloads, Updates (Regal + „Alle aktualisieren"), Runtimes, Diagnose, Einstellungen. Offen: Installationsgröße, Spielstandverwaltung |
| 15 Eingabe | Akeno selbst: DualSense, Tastatur, eigene Bildschirmtastatur. Spielbezogene Belegung übernehmen die Runtimes (z. B. MihawkOS' Controller-Modi) |
| 16 Grafik-Manager | offen: Akeno rendert keine Spiele; Auflösung und FPS stellen die Runtimes ein |
| 17 Downloads | umgesetzt: Warteschlange, Geschwindigkeit, Wiederaufnahme nach Abbruch (HTTP-Range), 3 Versuche, SHA-256, Platzprüfung, atomarer Tausch mit Rücksicherung, Aufräumen. Offen: parallele Downloads, Pause, Delta-Updates |
| 18 Stabilität/Sicherheit | umgesetzt: sichere Archivprüfung, kein Path Traversal, signierte Manifeste, TLS-Prüfung, nichts Fremdes wird überschrieben. Offen: Watchdog, Safe Mode |
| 19 Kompatibilitätsdatenbank | umgesetzt lokal mit Export/Import. Offen: freiwilliger, zentraler Abgleich |
| 20 Developer Mode | teilweise: eigene GitHub-Repositories, dokumentiertes Katalogformat, Provider-Schnittstelle. Offen: eigener Entwicklermodus in der Oberfläche |
