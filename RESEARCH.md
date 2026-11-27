# Recherche: Prospero-Ökosystem

Stand 27. November 2026. Alle Repositories wurden geklont und im Quellcode gelesen (Build-Skripte,
Runtime-Code, Lizenzen); Kataloge wurden live abgefragt. Hardware-Angaben sind Upstream-Berichte.

| Projekt (geprüfter Commit) | Lizenz | Befund | Nutzung in Akeno |
| --- | --- | --- | --- |
| [drakmor/ps5-native-app-boilerplate](https://github.com/drakmor/ps5-native-app-boilerplate) `470695e` | GPL-3.0-or-later | Öffentliche Toolchain für native Titel: LLVM 18, eigener PS5-Modul-Konverter und FSELF-Signierer, Clean-Room-`libc.prx`; Titelordner `eboot.bin`/`sce_sys`/`sce_module`. Upstream: FW 6.02 und 12.70. Lokal nachgebaut, `libc.prx`-Hash reproduziert. | Build-Grundlage (`ps5/tools/fetch-toolchain.sh`) |
| [darkxex/ProsperoStore](https://github.com/darkxex/ProsperoStore) `0e1f031` | GPL-3.0-or-later | Nativer Store auf derselben Basis: `sceHttp` im Sandbox-Betrieb, curl nach Elevation, Ed25519-Katalog, Installation nach `/data/homebrew`, ShadowMountPlus 1.7+ bindet `/data` ein, `_fstatfs`, Datei-Worker über Loader. | Katalogvertrag, `sceHttp`-Muster, Titelordner-Regeln, gepinnte Bibliotheken (Monocypher, yyjson, miniz, PicoSHA2, stb, qrcodegen, Inter) |
| [darkxex/ps5-homebrew-catalog](https://github.com/darkxex/ps5-homebrew-catalog) `5a0d9e9` | GPL-3.0 | API v1 Schema 3, signierte Manifeste, `content_version` für Updates, nur ZIP-Artefakte. Live: Sequenz 1293, 243 Apps. | ProsperoStore-Provider |
| [lowbit/ps5-openrct2](https://github.com/lowbit/ps5-openrct2) `e2d9a82` | GPL-3.0 | SDL2-Software-Renderer im nativen Titel (FW 13.00, 1080p); Runtime für Heap, `opendir`, `realpath`, `exit`; SDL-Patches (IME-Laden, Touchpad). | Titel-Runtime und SDL-Build (`ps5/runtime`, `ps5/patches/sdl`) |
| [mateo-cogeanu/ProsperoCraft](https://github.com/mateo-cogeanu/ProsperoCraft) `90b6b61` | GPL-3.0-or-later | Minecraft Java mit nativer Java-25-Runtime, Vulkan, Microsoft-Login; Release `PPSA99874.zip`. | Über den signierten Katalog installierbar |
| [Shabbypenguin/PokeMMO-Prospero](https://github.com/Shabbypenguin/PokeMMO-Prospero) `aa3ff9c` | GPL-3.0-or-later | Offizieller Linux-Client nativ; Client wird vom Spiel selbst geladen, ROMs vom Nutzer. FW 12.40. | Über den Katalog installierbar (`PPSA98001`) |
| [mihawk-99/PS5_MihawkOS](https://github.com/mihawk-99/PS5_MihawkOS) `05a95de` | GPL-3.0 | Wine/GE-Proton/RADV; `profile=`/`path=`-Startargumente; Forwarder-Kacheln über ELF-Loader (Titel können keine Titel starten). | Windows-Runtime; Designvorlage für Starts und Verknüpfungen |
| [kvaps/ProsperoWeb](https://github.com/kvaps/ProsperoWeb) `8377531` | GPL-3.0 (WebKit eigene Lizenzen) | WebKit-Browser als Titel; Argumente nur `--env=…`, keine Start-URL. | Web-Runtime (ohne Direktstart einer URL) |
| [mpereiraesaa/prospero-win](https://github.com/mpereiraesaa/prospero-win) `18115ce` | LGPL-2.1-or-later | Wine mit x86-Übersetzer, DXVK, Zink. | Windows-Runtime (Katalog `PPSA99995`) |
| [Rufidj/ps5link-sdk](https://github.com/Rufidj/ps5link-sdk) `ea771e5` | GPL-3.0 | Alternativer C-Linker (aus SharpProspero) mit AGC-Shadern; FW 9.00. | Nicht benötigt (Boilerplate-Konverter genügt) |
| [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk) `4122b71` | GPL-3.0 | Payload-SDK; liefert Header, Stubs, libc++. v0.42 über die Boilerplate gepinnt. | Compiler-Sysroot, Payload-Build |
| [prosperostore.net](https://prosperostore.net/) | — | Website und API des Katalogs. | Live-Quelle |
| [recompstore.com](https://www.recompstore.com/) | — | `catalog.json` mit 162 Einträgen, Quelle jeweils GitHub-Repo + Asset-Muster, meist ohne SHA-256; eigene Konsolen-App elripalda/RecompStore. | RecompStore-Provider (nur mit GitHub-Digest) |

## Wichtige Plattform-Erkenntnisse

- **Titel starten keine Titel.** `sceLncUtilLaunchApp` ist für einen Titel ein Null-Import,
  `sceSystemServiceLaunchApp` antwortet `0x8094000f` (MihawkOS). Lösung: Payload am ELF-Loader.
- **Nicht geladene Module.** `libScePosixForWebKit`, `libkernel_sys`, `libkernel_web` werden in einem
  Spielprozess nicht geladen; Importe daraus springen ins Leere (ProsperoStore, OpenRCT2). `statfs` liegt
  dort; libkernel exportiert `_fstatfs`.
- **Sandbox-Netz.** Im Sandbox-Titel nutzt man `sceHttp`; libcurl/BSD-Sockets erst nach Elevation.
- **Dateirechte.** Titelordner brauchen `0777`, sonst CE-107750-0.
- **Schreibdrosselung.** Ein Titel wird nach einigen hundert MB auf ~2 MB/s gedrosselt (ProsperoStore
  misst das; Loader-Prozesse sind nicht gedrosselt). Akeno entpackt derzeit im eigenen Prozess.
