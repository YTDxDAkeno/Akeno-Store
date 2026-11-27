# Verwaltete lokale Prozesse auf dem Entwicklungshost

`runtimes.managed.ManagedRuntime` betreibt zwei Prozess-Backends für ausdrücklich ausgewählte lokale Programme. Sie laufen mit den Rechten des angemeldeten Linux-Benutzers. Verwende dafür ein Verzeichnis mit selbst erstellten oder von dir geprüften Dateien. Im SDK und in der CLI ist der Pfad zum vertrauenswürdigen Verzeichnis ein Pflichtargument. Browser und Store können diese Backends nicht aufrufen.

| Backend | Voraussetzung | Prüfergebnis |
| --- | --- | --- |
| `java-21` | Java 21, ausführbares JAR mit Main-Class | Zwei unabhängig kompilierte Programme auf dem Linux-Host ausgeführt |
| `java-17`, `java-25` | Passende lokal installierte Java-Version | Erkennung implementiert, Ausführung in dieser Umgebung offen |
| `linux-elf` | Ausführbares ELF64 für die Architektur des Hosts, x86-64 oder AArch64 | Zwei kleine x86-64-Programme kompiliert und ausgeführt |

## Lokaler Aufruf

Weise `--trusted-root` ein bereits vorhandenes Verzeichnis zu. Der Einstiegspfad ist relativ zu diesem Verzeichnis. Das erste Kommando listet die auf diesem Rechner erkannten Backends auf.

```sh
python3 -m runtimes.managed --trusted-root ./meine-programme
python3 -m runtimes.managed --trusted-root ./meine-programme --timeout 180 java-21 Beispiel.jar
python3 -m runtimes.managed --trusted-root ./meine-programme linux-elf Beispiel
python3 -m runtimes.managed --trusted-root ./meine-programme --safe-mode
```

Mit `--java-home /pfad/zum/jdk` kannst du zusätzliche lokal geprüfte Java-Installationen angeben. Der Manager ermittelt die Version durch den Aufruf von `java -version`. `--disable java-21` sperrt ein einzelnes Backend für diesen Aufruf. `--safe-mode` schaltet alle Prozessstarts und JVM-Erkennungsaufrufe aus. Die Persistenz dieser Optionen liegt beim Aufrufer.

## Prozesssteuerung und Beobachtungen

`run(backend, entry, timeout=120)` überwacht eine gestartete Prozessgruppe. Ein zweiter Start über denselben Manager wird während der Laufzeit abgelehnt. Der Aufrufer kann `status` lesen und aus einem anderen Thread mit `stop()` das Ende anfordern. Zeitlimit und Stop senden SIGTERM, nach einer halben Sekunde folgt bei Bedarf SIGKILL. Zurückbleibende Prozesse derselben Gruppe werden beim Ende des Hauptprozesses beendet.

Das Ergebnis enthält Zustand, Exitcode, die letzten 8192 Ausgabebytes als Text und den höchsten beobachteten RSS-Wert des Hauptprozesses in KiB. Sehr kurze Programme können zwischen den Stichproben enden. Das Arbeitsverzeichnis ist das Verzeichnis der Startdatei. Konfiguration, Spielstände und die Lizenz des Programms liegen beim Benutzer. Der Manager bietet weder Betriebssystemisolierung noch eine Speicherobergrenze.

```sh
python3 -m unittest discover -s tests -p test_managed.py -v
```

Die Tests belegen Prozessausführung und Überwachung auf dem Host. Die Plattformadapter für Java und native Prozesse auf der PS5 sowie die Spiele- und Hardwareprüfung stehen aus.

Mit `auto` statt einer Backend-ID kann der Supervisor für das ausgewählte Programm im vertrauenswürdigen Verzeichnis Java anhand der Klassenversion oder Linux ELF anhand von ABI, Architektur und bekannten Bibliotheken auswählen. Der Linker kann zur Laufzeit zusätzliche Bibliotheken verlangen.

```sh
python3 -m runtimes.managed --trusted-root /pfad/zu/eigenen-spielen auto spiel.jar
```

Die Host-Tests wurden mit Temurin 17.0.16, OpenJDK 21 und Temurin 25.0.1 durchgeführt. Jede Version hat zwei dafür kompilierte JARs gestartet. Der Test prüft auch, welche Java-Version `auto` für jede JAR auswählt. Über `AKENO_TEST_JAVA_HOMES` lassen sich zusätzliche JDK-Verzeichnisse als durch Doppelpunkte getrennte Liste angeben.
