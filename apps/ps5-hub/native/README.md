# Nativer SDL2-Katalog

Die Anwendung öffnet einen Katalog und optional einen Bibliotheksstand aus zwei lokalen TSV-Dateien. Die SDL2-Oberfläche zeigt Quelle, Version und Spiel-ID. Katalogexport, Downloads und Spielstarts laufen über die Host-Anwendung.

```sh
make native
dist/native/akeno-hub katalog.tsv bibliothek.tsv
make native-test
```

Der Host-Build benötigt einen C11-Compiler und die SDL2-Entwicklungsdateien. Falls SDL2 außerhalb des üblichen Suchpfads installiert ist, können `SDL2_CFLAGS` und `SDL2_LIBS` beim Build gesetzt werden. Der SDL2-Selbsttest prüft die Darstellung intern mit einem Software-Renderer und verwendet simulierte Controller-Ereignisse.

## Dateiformat

Jede Zeile enthält vier tabulatorgetrennte Spalten ohne Kopfzeile. Die Spalten sind Spiel-ID, Titel, Quelle und Version. Die ersten drei Spalten sind Pflicht. Für eine unbekannte Version bleibt die letzte Spalte leer. Leere Dateien sind gültig. Pro Datei können bis zu 1024 Zeilen gelesen werden. Maximale Feldlängen in Bytes sind 95, 255, 95 und 95. Die integrierte Schrift zeigt Buchstaben, Ziffern und einige Satzzeichen an. Weitere Unicode-Zeichen erscheinen als Fragezeichen, und lange Texte werden an der Ansichtsgrenze abgeschnitten.

```sh
printf 'signal-drift\tSignal Drift\tAkeno\t1.0.0\n' > katalog.tsv
```

Mit Pfeiltasten oder Steuerkreuz lässt sich die Auswahl verschieben. Tab, Links/Rechts oder L1/R1 wechseln die Ansicht. Enter oder Kreuz/A öffnet die Details. Escape oder Kreis/B geht zurück und beendet die Anwendung beim erneuten Drücken auf der Katalogansicht.

`make ps5` erwartet die in [platform/ps5](../../../platform/ps5/README.md) beschriebenen Zielwerkzeuge. Eine Version für PS5-Hardware wurde in dieser Cloud-Umgebung noch nicht validiert.

F1 öffnet die Suche nach Titel oder Quelle. F2 und Dreieck/Y wechseln zwischen allen Einträgen, PS5 und PC. Die Suche übernimmt Tastatureingaben. Der Plattformfilter funktioniert auch mit einem Controller. Ein für das Ziel kompiliertes Programm liest beim Start ohne Argumente die beiden Snapshots aus `/app0/assets`.
