# Audit der Codex-Codebasis (27. November 2026)

Geprüft wurde der Stand `046cedc` (Branch `codex/akeno-native-alpha`). Der Stand ist als
Git-Tag `checkpoint/codex-baseline` erhalten. Die Tabelle nennt jeden Befund und was geändert wurde.

## Architektur und falsche Annahmen

| Befund | Schwere | Behebung |
| --- | --- | --- |
| Annahme, für `eboot.bin` sei ein „autorisiertes SDK" nötig. Der PS5-Pfad endete bei einem gelinkten `.so` ohne Startcode; `build/ps5_release.py` prüfte nur Ordner, die ein solches SDK hätte liefern sollen. | kritisch | Öffentliche Toolchain der ps5-native-app-boilerplate übernommen (Payload-SDK v0.42, `ps5-native-tool`, Clean-Room-`libc.prx` mit hardwaregetestetem Hash). `ps5/build.sh` erzeugt einen signierten Titelordner und ein ZIP. Alte Pipeline entfernt. |
| Die Konsolen-App war ein TSV-Betrachter mit 5×7-Bitmap-Schrift; Katalog, Downloads und Installation liefen nur im Python-Host, der auf der PS5 nicht läuft. | kritisch | Neuer nativer C++-Kern (Stores, HTTPS, Installer, Bibliothek, Runtimes) und neue SDL2-Oberfläche, identischer Code für Host und PS5. |
| PacBrew-SDL2 im Titel: initialisiert Tastatur/IME ohne geladenes Modul (Absturz laut OpenRCT2-Port), `strcasestr` würde aus dem nicht geladenen `libScePosixForWebKit` aufgelöst. | hoch | SDL2 aus ps5-payload-dev-Quelle mit den OpenRCT2-Patches; Linker schließt Module aus, die ein Spielprozess nicht lädt (fehlerhafte Importe brechen den Build ab). |
| Kein Heap-, `opendir`-, `realpath`- und `exit`-Ersatz für den Titelkontext. | hoch | Runtime von lowbit/ps5-openrct2 (GPL-3.0) übernommen. |
| Rückkehr aus `main` beendet einen Titel per Absturz. | hoch | `platform::quit()` beendet über `sceSystemServiceLoadExec("exit")` und kehrt nie zurück. |

## Stores

| Befund | Behebung |
| --- | --- |
| ProsperoStore: 10 fest codierte Title-IDs mit handgeschriebenen Beschreibungen statt des Katalogs (live: 243 Apps). Keine Signaturprüfung. | Vollständiger signierter Index, Ed25519 (beide Schlüssel), Sequenz-Rollback-Schutz, SHA-256 jeder API-Datei; nativ und im Python-Host. |
| GitHub: fest codierte Projektliste inkl. Beschreibungen. | Native App: Releases nur über Asset-Digest; eigene Repositories in den Einstellungen. Projekte, die im signierten Katalog stehen, kommen von dort. |
| RecompStore, Game Jolt, GOG, Epic fehlten („keine nachgewiesene Schnittstelle"). | Öffentliche Endpunkte gefunden und live geprüft; RecompStore installierbar über GitHub-Digest, die anderen als ehrliche Entdecken-Einträge bzw. Browserspiele. |
| itch.io-Feed-URL veraltet (`price-free.xml` → 301). | Kanonische Feeds `free.xml` und `free/platform-web.xml`. |

## Code und Build

| Befund | Behebung |
| --- | --- |
| `build/package_host.py` verwies auf sieben nicht existierende Ordner. | Liste korrigiert, Build- und Abhängigkeitsordner ausgeschlossen. |
| `PureWindowsPath.is_reserved()` ist ab Python 3.15 entfernt (DeprecationWarning). | Eigene Prüfung reservierter Windows-Namen. |
| Binärartefakte wurden in einen Git-Branch committet (`codex/akeno-native-alpha-artifacts`). | GitHub-Actions-Workflow baut und veröffentlicht das ZIP als Artefakt. |
| Kein Lizenztext im Repository, obwohl GPL-Komponenten eingeplant waren. | `LICENSE` (GPL-3.0); Akeno-Code GPL-3.0-or-later, da `eboot.bin` GPL-Runtime-Code linkt. |
| Dokumentation stellte Recherche-Notizen als Projektstatus dar und nannte Blocker, die keine waren. | Dokumentation neu, Status strikt nach Stufen ([STATUS.md](../STATUS.md)). |

## Erhalten

Der Python-Host-Hub (Web-UI, HTML5-Player, Signal Drift, geprüfte ZIP-Installation, Kompatibilitäts-
datenbank, Signatur-Erkennung, Java/ELF-Host-Runner) bleibt als Entwicklungswerkzeug erhalten; alle
52 Tests laufen. Die Erkennungslogik aus `compatibility/inspection.py` diente als Vorlage für
`native/src/runtime/runtimes.cpp`.
