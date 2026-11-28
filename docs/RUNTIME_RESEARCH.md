# Wie PS5-Homebrew Java-, Linux- und Windows-Programme ausführt

Quellcode-Recherche vom 28. November 2026 als Grundlage für Akenos eigene Runtimes. Gelesen wurden
die Repositories selbst (Stand in Klammern), nicht nur ihre Beschreibungen.

## Gemeinsames Muster: ein Spiel pro Prozess, Neustart des eigenen Titels

Prospero Win (`wine64_main.c`) und MihawkOS (`pw_wine_launch.h`) zeigen beim Start durch das System
ihren eigenen Launcher. Für ein Spiel starten sie **ihren eigenen Titel** mit
`sceSystemServiceLoadExec("/app0/eboot.bin", argv)` und den Argumenten des Spiels neu; endet das
Spiel, starten sie wieder in den Launcher. Jedes Spiel bekommt so einen frischen Prozess.

**Akeno übernimmt genau dieses Muster** (`native/src/play/`): „Spielen" startet Akeno mit
`--play=<id>` neu, die eingebaute Runtime übernimmt Bild, DualSense und Ton, danach startet Akeno mit
`--return=<id>` zurück in den Hub. Eine Hauptmenü-Kachel startet Akeno direkt mit `--play=<id>`.
Es wird keine andere App geöffnet.

## Windows: Prospero Win (`mpereiraesaa/prospero-win`, `18115ce`, LGPL-2.1-or-later)

- Wine läuft **im Titel selbst**: Der Titel lädt `ntdll.prx` (Wines Unix-Seite, als PS5-PRX gebaut)
  und startet `__wine_main` in einem eigenen Thread. Die PS5-Treiber von Wine übergeben Bilder an
  den Titel (VideoOut), lesen das Pad (XInput-Patch) und mischen Ton in den AudioOut-Port.
- 64-Bit-Windows-Code läuft direkt auf der x86-64-CPU; 32-Bit-Code über einen eigenen
  Binärübersetzer (`wowprospero`, Wines WoW64-CPU-Backend).
- Direct3D 8–11 über DXVK, OpenGL über Mesa/Zink, beides auf Vulkan (RADV).
- `/data` wird über einen einmaligen Helfer am ELF-Loader angefordert (`pw_data_mount`); Akeno hat
  `/data` bereits über ShadowMountPlus.
- Die Wine-Laufzeit liegt im Titel unter `/app0/win/wine/lib/wine/x86_64-unix`; Paketgröße laut
  ProsperoStore 171 MB.

**Für Akeno:** Die Windows-Runtime wird nach diesem Aufbau in Akenos eigenen Titel eingebaut
(LGPL-2.1 ist mit Akenos GPL-3.0 vereinbar). Dazu gehören die Wine-PRX-Module, die Treiber-Senken für
VideoOut/Pad/Audio und der i386-Übersetzer. **Noch nicht integriert.** Es braucht die Wine-Module
(rund 170 MB) und mehrere Konsolentests. Die Module müssen außerdem aus `/data` statt aus `/app0`
laden.

MihawkOS (`mihawk-99/PS5_MihawkOS`, `05a95de`, GPL-3.0) setzt dasselbe Prinzip größer um: Wine +
GE-Proton, Mesa/RADV/Zink, Hilfsprozesse für Grafik und Dienste, rund 1,2 GB Laufzeit. Es dient als
Referenz für Spielprofile (`game.json`), Speicherverwaltung (16-KiB-Seiten, Direct Memory) und
bekannte Spielprobleme (`docs/PROTON_GAME_KNOWLEDGE.md`).

## Linux-ELF: PokeMMO Prospero (`Shabbypenguin/PokeMMO-Prospero`, `aa3ff9c`, GPL-3.0)

- **Keine JVM**: PokeMMOs Linux-Client ist ein GraalVM-Native-Image (eine Linux-x86-64-ELF).
- Ein kleiner C-Loader (aus PokeMMO-NX, MIT) bildet die ELF ab, löst Relokationen und
  Symbolversionen auf und beantwortet die glibc-Aufrufe mit einer **Linux-ABI-Schicht** (Dateien,
  Threads, Sync, Speicher, Sockets, epoll, dlopen, TLS über `__tls_get_addr`).
- SDL3, OpenAL, EGL/GL und GTK-Teile werden ersetzt; OpenGL kommt von
  [ps5-opengl](https://github.com/darkxex/ps5-opengl) (Mesa).
- PS5-Besonderheiten: keine RWX-Seiten (erst schreiben, dann `mprotect` auf RX), 16-KiB-Seiten,
  `opendir` liefert EPERM (stattdessen `getdents`), `getaddrinfo` stürzt ab (stattdessen
  `sceNetResolver`).

**Für Akeno:** Das ist der Weg zu einer allgemeinen Linux-Runtime (auch für Godot- und
Unity-Linux-Exporte). Der Loader deckt bisher nur die Bibliotheken ab, die PokeMMO braucht; jede
weitere Engine braucht ihre eigenen Importe. **Noch nicht integriert.**

## Java: ProsperoCraft und ps5-java25-homebrew (`mateo-cogeanu`, `90b6b61` / `95fb41d`)

- ProsperoCraft nutzt eine separat gepflegte **native OpenJDK-25-Runtime** (FreeBSD-OpenJDK-Port,
  GPL-2.0 mit Classpath-Ausnahme), die im Titel läuft. Die Grafik kommt über Vulkan (RADV), LWJGL
  ist für die PS5 gebaut.
- Belegt sind auf der Konsole (Firmware 13.60): VM-Start, Threads, Dateien, ZIP/JAR, JNI, HTTPS –
  im **Interpreter-Modus**. JIT und eine allgemeine Desktop-Java-Kompatibilität sind ausdrücklich
  nicht belegt.
- Gebaut wird die Runtime auf macOS mit LLVM 22 aus rund 1 GB OpenJDK-Quellen.

**Für Akeno:** Eine Java-Runtime (Mindustry, Stendhal-Client, libGDX-Spiele) würde diese Runtime in
Akenos Spielmodus einbetten. **Noch nicht integriert.** Die Einbettung in fremde Titel ist laut
Projekt selbst noch nicht abgeschlossen.

## HTML5: ProsperoWeb (`kvaps`, `8377531`) und Parallax Browser (`rikleigh`, `bf5ae25`, GPL-3.0)

- Beide sind WebKit-Ports (WPE). Parallax bringt WebGL, WebGPU und die Gamepad-API mit und läuft mit
  Hilfsprozessen über Payload-Routen.
- **Für Akeno:** Eine HTML5-Runtime ohne Browser-Oberfläche braucht dieselbe Engine, gebaut für
  Akenos Spielmodus, mit einer lokalen Seite (`file://…/index.html`) im Vollbild. **Noch nicht
  integriert.** Bis dahin sind Browserspiele in Akeno als „nicht kompatibel" markiert.

## RPG Maker 2000/2003: EasyRPG Player – **integriert**

- Upstream [EasyRPG Player](https://github.com/EasyRPG/Player) 0.8.1.1 (GPL-3.0) mit liblcf 0.8.1
  (MIT); der PS5-Port von plutobox ([ps5-Towelket](https://github.com/plutobox/ps5-Towelket),
  `b491f7b`) zeigt, dass der Player auf der Konsole läuft (Towelket, PPSA92046).
- Akeno baut den Player aus gepinnten Quellen (`native/tools/easyrpg.sh`) mit SDL2 (Software-
  Renderer, VideoOut), SDL-Audio und den eingebauten WAV/MIDI-Decodern und linkt ihn in sein eigenes
  `eboot.bin`. Für die PS5-libc ergänzt `ps5/runtime/libc.c` die C-Locale-Funktionen, die libc++
  verlangt (`newlocale`, `strcoll_l` …).
- Auf dem Linux-Host läuft das EasyRPG-TestGame (GPL-3.0) im Akeno-Spielmodus. **Auf der PS5 ist der
  Spielmodus noch nicht getestet.**

## Recompilation und Ports

RecompStore und ProsperoStore listen statische Recompilations (N64Recomp-, XenonRecomp-Ports usw.) als
native PS5-Titel. Diese laufen direkt; die eigenen Originaldaten liest jedes Spiel selbst ein. Akeno
installiert und startet sie und entwickelt keinen eigenen Recompiler.

## Was daraus für die Reihenfolge folgt

1. RPG Maker 2000/2003 (EasyRPG) – eingebaut, Konsolentest steht aus.
2. Windows x64 nach Prospero Win – größter Nutzen (GOG, itch.io, Unity, GameMaker), hoher Aufwand.
3. Linux-ELF nach dem PokeMMO-Loader – für Godot/Unity-Linux-Exporte.
4. Java (OpenJDK 25) – für Mindustry, libGDX.
5. HTML5 (WebKit) – für Browserspiele und RPG Maker MV/MZ.
