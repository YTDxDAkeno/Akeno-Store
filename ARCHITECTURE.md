# Architektur

Akeno besteht aus einer nativen C++20-App, die unverändert auf dem Linux-Host und als PS5-Titel läuft,
und dem älteren Python-Host-Hub als Entwicklungswerkzeug.

```
             ┌──────────────────────── native/src ─────────────────────────┐
 Eingabe ──▶ │ ui/app      Tabs, Detailseite, Tastatur, Menüs, Toasts       │
 (DualSense, │ ui/gfx      Text (stb_truetype), Formen, Verläufe (SDL2)     │
  Tastatur)  │ ui/images   Cover laden, cachen, zuschneiden (Hintergrund)   │
             ├──────────────────────────────────────────────────────────────┤
             │ stores/     Provider ─▶ Catalog (parallel, zusammengeführt)  │
             │ install/    Installer (Jobs) ─▶ Library (ShadowMountPlus)    │
             │ runtime/    Erkennung ─▶ Runtime-Titel ─▶ Start/Verknüpfung   │
             │ core/ net/  JSON, SHA-256/Ed25519, ZIP, Dateien, HTTP-Policy │
             ├──────────────────────────────────────────────────────────────┤
             │ platform/   host.cpp + libcurl   │  ps5.cpp + sceHttp         │
             └──────────────────────────────────────────────────────────────┘
```

## Datenfluss

1. **Stores.** Jeder Provider liefert normalisierte `Game`-Einträge mit einer Zugriffsart:
   `Install` (PS5-Titel-ZIP mit bekanntem oder auflösbarem SHA-256), `Runtime` (läuft in einem
   Runtime-Titel), `External` (nur Store-Seite, z. B. GOG/Epic) und `Upcoming`. Nichts wird vorgetäuscht:
   ein Eintrag ohne geprüften Download bietet keine Installation an.
2. **Catalog** lädt alle aktivierten Provider parallel und führt Einträge mit gleicher Title-ID
   zusammen. Der signierte ProsperoStore-Eintrag behält seine Installationsdaten; RecompStore ergänzt
   Hero-Bilder und „benötigt eigenes Original".
3. **Installer** arbeitet Aufträge nacheinander ab: Download in `<Laufwerk>/akeno-work` mit laufendem
   SHA-256, Vergleich mit dem Katalog, genau ein Titelordner im ZIP (oben oder bis zu drei Ebenen tief),
   Title-ID-Abgleich, Entpacken mit Pfad-, Link- und Größenprüfung, Lauf-Schutz über `/mnt/sandbox`,
   Umbenennen in den ShadowMountPlus-Ordner (vorherige Version wird gesichert), Rechte `0777`, Beleg.
4. **Library** liest `sce_sys/param.json` aller Titelordner und vergleicht `contentVersion` mit dem
   Katalog (`NN.NNN.NNN`) für Updates.
5. **Runtimes** erkennen eigene Dateien anhand von Signaturen und routen zu PS5-Runtime-Titeln aus dem
   Katalog. Starten und Hauptmenü-Kacheln laufen über eine Payload am ELF-Loader, weil ein Titel keinen
   anderen Titel starten kann.

## Plattformschicht

| | Linux-Host | PS5-Titel |
| --- | --- | --- |
| HTTPS | libcurl | `sceHttp` mit Zertifikats- und Hostprüfung (wie ProsperoStore im Sandbox-Kontext) |
| Daten | `~/.local/share/akeno-native` | `/data/akeno`, sonst `/download0/akeno` |
| Installationsziele | simulierter `/data/homebrew` | `/data/homebrew`, `/mnt/ext0|ext1/homebrew`, `/mnt/usbN/homebrew` |
| Grafik | SDL2 mit GPU-Renderer | SDL2 VideoOut-Framebuffer, Software-Renderer |
| Beenden | `exit` | `sceSystemServiceLoadExec("exit")` |
| Start anderer Titel | nicht möglich | Payload an `127.0.0.1:9021` |

## PS5-Build

`ps5/build.sh` kompiliert mit Clang 18 für `x86_64-sie-ps5`, linkt mit der Titel-Runtime, libc++ und
SDL2, konvertiert mit `ps5-native-tool` und signiert als FSELF. Details in [BUILD.md](BUILD.md).

## Python-Host-Hub

`core/server.py` stellt eine lokale Web-Oberfläche bereit (HTML5-Player in einer Sandbox, Signal Drift,
geprüfte HTML5-ZIP-Installationen, Kompatibilitätsdatenbank, Java/ELF-Prozessaufsicht für lokale
Tests). Er nutzt dieselbe ProsperoStore-Signaturprüfung (pure-Python-Ed25519) und bleibt ein
Entwicklungswerkzeug; auf der PS5 läuft die native App.
