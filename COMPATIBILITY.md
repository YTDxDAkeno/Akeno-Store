# Kompatibilität

| Ziel | Aktueller Status |
| --- | --- |
| Linux-Host und Chromium | Hub, ZIP-Installation und HTML5-Player |
| Lokale HTML-, CSS- und klassische JavaScript-Dateien | Unterstützter Einstiegspunkt für vollständig lokale Spiele |
| Java, Windows, Linux-Binärdateien, Godot, Ren’Py, RPG Maker 2000/2003 | Keine aktive Laufzeit |
| WebAssembly, WebSockets, persistenter Browser-Speicher im Spiel, volle RPG-Maker-MV/MZ-Laufzeit | Stehen aus |
| PS5-Firmware, Gerätecontroller, Grafik- und Audio-APIs | Hardwaretests stehen aus |

Die Kompatibilitätsdatenbank protokolliert Beobachtungen auf dem Host pro Spielversion und Runtime-Version. Eine erfolgreich ausgelieferte Player-Seite belegt keinen Spielstart. Die Datenbank setzt deshalb den Gameplay-Status auf `unconfirmed`. Die Engine-Hinweise für RPG Maker, Java, Ren’Py, Godot und Windows machen kein Paket ausführbar.

## Lokale Dateierkennung

```sh
python3 -m compatibility /pfad/zum/spiel --platform ps5
```

Die Ausgabe nennt beobachtete Dateisignaturen und erkannte Anforderungen. Die Auflösung berücksichtigt Plattform, Architektur, vorhandene Backends und bekannte Abhängigkeiten. ROM- und ISO-Signaturen beschreiben das Format der Datei. Sie belegen keine startfähige Recompilation. [Host-Runtimes](docs/RUNTIMES.md)
