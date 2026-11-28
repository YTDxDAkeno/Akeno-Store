# Abdeckung des Master-Prompts

Stand 28. November 2026, Akeno `01.004.000`. Diese Liste beantwortet „wo ist was" ehrlich: **umgesetzt**
heißt im Code vorhanden und auf dem Linux-Host getestet; auf der PS5 belegt ist nur, was
[STATUS.md](../STATUS.md) so ausweist. **Teilweise** und **offen** nennen den Grund.

## Architektur (Abschnitt 4)

| Baustein | Stand | Wo |
| --- | --- | --- |
| Akeno Core | umgesetzt | `native/src/core`, `native/src/platform` |
| Store Engine | umgesetzt (8 Provider) | `native/src/stores` |
| Runtime Manager | umgesetzt: eingebaute Akeno-Runtimes mit Stand und Grundlage; Spielmodus im eigenen Titel | `native/src/runtime/runtimes.cpp`, `native/src/play/`, Tab „Runtimes" |
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
| Akeno-Katalog | 38 kuratierte Spiele aus Abschnitt 9 mit ehrlichem Status (nativ spielbar oder fehlende Akeno-Runtime) |
| itch.io | teilweise: RPG-Maker-2000/2003-Spiele über die offizielle API mit eigenem Schlüssel installieren und spielen; Rest zum Entdecken. **Offen:** Bibliothek gekaufter Spiele, Updates |
| Game Jolt | teilweise: Entdecken, Browserspiele |
| GOG | teilweise: kostenlose Spiele entdecken; spielbar erst mit Akenos Windows-Runtime |
| Epic Games | teilweise: aktuelle Gratis-Aktionen entdecken; kein Konto (keine offizielle Drittanbieter-Anbindung) |
| Steam | offen: Steam-Spiele bleiben an den Steam-Client gebunden; frühestens mit Akenos Windows-Runtime |
| Amazon, Humble | offen: keine öffentliche Schnittstelle ohne Konto-Autorisierung |
| Flathub, Lutris | offen: liefern Linux-Pakete bzw. Linux-Installationsskripte; eine allgemeine Linux-Runtime gibt es auf der PS5 nicht |
| Mod DB | offen |

## Runtimes (Abschnitte 7 und 8)

Spiele laufen in Akeno selbst (Spielmodus); Weiterleitungen an andere Apps gibt es nicht.

| Runtime | Stand |
| --- | --- |
| RPG Maker 2000/2003 | **eingebaut** (EasyRPG Player 0.8.1.1 im `eboot.bin`), Host getestet, PS5 offen |
| Native PS5-Titel | eingebaut |
| Windows x86/x64 | geplant nach Prospero Win (Wine im eigenen Titel, LGPL-2.1) |
| Linux-ELF | geplant nach dem PokeMMO-Prospero-Loader (Linux-ABI, ps5-opengl) |
| Java/JVM/LWJGL | geplant auf Basis des OpenJDK-25-Ports (ps5-java25-homebrew) |
| HTML5/JS/WASM | geplant: WebKit ohne Browser-Oberfläche |
| Godot, Unity, Ren'Py, Python, .NET, GameMaker | geplant (eigene Engines bzw. über die Windows/Linux-Runtime) |
| ROMs/ISOs | geplant: libretro-Kerne im Spielmodus; Recomp-Ports aus RecompStore laufen nativ |
| Engine-Erkennung | umgesetzt: Engine, zuständige Runtime, spielbar ja/nein |
| Automatische Installation | umgesetzt für eingebaute Runtimes: Download → Erkennung → Startprofil → Spielen |

Grundlagen und Aufwand je Runtime: [RUNTIME_RESEARCH.md](RUNTIME_RESEARCH.md).

## Spiele (Abschnitt 9)

Alle genannten Spiele stehen im Akeno-Katalog (`native/assets/catalog/akeno.json`), jedes mit dem
tatsächlichen Weg auf die PS5:

- **Nativ im signierten ProsperoStore:** PokeMMO, Minecraft (ProsperoCraft), SuperTuxKart, osu!,
  dazu Singleplayer-Spiele wie Dungeon Crawl Stone Soup, NetHack, Brogue, Celeste Classic, DOOM.
- **Browserspiele (warten auf die HTML5-Runtime):** Pokémon Showdown, PokéRogue, TETR.IO, Stendhal.
- **Mit Begründung, welche Akeno-Runtime noch fehlt:** Luanti, OpenRA, Sonic Robo Blast 2, Ring Racers, Warzone 2100, Xonotic, OpenArena,
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
| 13 Hauptmenü | umgesetzt: Kachel pro Bibliotheksspiel startet Akeno direkt im Spielmodus (Konsolentest offen) |
| 14 Oberfläche | umgesetzt: Entdecken, Alles, Kostenlos, Online & Multiplayer, Singleplayer, Fanprojekte, Indie, PS5-Titel, Recomps, Emulatoren, Browser, Windows, Visual Novels, Getestet, Store-Provider, Bibliothek, Downloads, Updates (Regal + „Alle aktualisieren"), Runtimes, Diagnose, Einstellungen. Offen: Installationsgröße, Spielstandverwaltung |
| 15 Eingabe | Akeno selbst: DualSense, Tastatur, eigene Bildschirmtastatur. In der RPG-Maker-Runtime: DualSense über SDL; eigene Belegung im Player-Menü |
| 16 Grafik-Manager | offen: Akeno rendert keine Spiele; Auflösung und FPS stellen die Runtimes ein |
| 17 Downloads | umgesetzt: Warteschlange, Geschwindigkeit, Wiederaufnahme nach Abbruch (HTTP-Range), 3 Versuche, SHA-256, Platzprüfung, atomarer Tausch mit Rücksicherung, Aufräumen. Offen: parallele Downloads, Pause, Delta-Updates |
| 18 Stabilität/Sicherheit | umgesetzt: sichere Archivprüfung, kein Path Traversal, signierte Manifeste, TLS-Prüfung, nichts Fremdes wird überschrieben. Offen: Watchdog, Safe Mode |
| 19 Kompatibilitätsdatenbank | umgesetzt lokal mit Export/Import. Offen: freiwilliger, zentraler Abgleich |
| 20 Developer Mode | teilweise: eigene GitHub-Repositories, dokumentiertes Katalogformat, Provider-Schnittstelle. Offen: eigener Entwicklermodus in der Oberfläche |
