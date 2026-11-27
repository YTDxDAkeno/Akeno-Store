# Änderungen

## 00.002.000 – Native PS5-Titel (27. November 2026)

- Neuer nativer C++20-Hub (SDL2) mit dunkelblau-violettem Design, großen Covern, Hero-Banner,
  Regalen, Store-Raster mit Filtern, Bildschirmtastatur, Detailseiten, QR-Codes, Toasts und
  DualSense-Glyphen; identischer Code für Host und PS5.
- PS5-Build mit öffentlichen Werkzeugen: signiertes `eboot.bin`, `libc.prx`, `sce_sys`,
  Installations-ZIP `PPSA90410.zip`; GitHub-Actions-Artefakt.
- Stores: vollständiger signierter ProsperoStore-Katalog (Ed25519, Rollback-Schutz), RecompStore
  (GitHub-Digest), itch.io, Game Jolt, GOG, Epic.
- Download-Manager und Installer mit SHA-256, ZIP-Prüfung, atomarem Tausch, Updates, automatischen
  Updates, Deinstallation und Lauf-Schutz.
- Runtime-Router für ROMs, Disc-Images, Windows-, Flash-, J2ME- und HTML5-Inhalte zu vorhandenen
  PS5-Runtime-Titeln; Starts und Hauptmenü-Verknüpfungen über den ELF-Loader.
- Audit der Codex-Basis ([docs/AUDIT.md](docs/AUDIT.md)); überholte PS5-Pipeline entfernt;
  Python-Host nutzt jetzt ebenfalls den signierten Katalog.

## 0.1.0 – Host-Entwicklungsbuild (Codex)

- Lokaler Web-Hub mit Katalog, Bibliothek, HTML5-Player, Signal Drift und Kompatibilitätsdatenbank.
