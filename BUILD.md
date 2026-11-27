# Build und Tests

## PS5-Titel (`PPSA90410`)

Linux oder WSL (Ubuntu 24.04) mit Clang/LLD/LLVM 18, compiler-rt, CMake, Ninja, Python 3 mit Pillow,
Git, curl, wget und unzip:

```sh
sudo apt install clang-18 lld-18 llvm-18 libclang-rt-18-dev cmake ninja-build python3-pil git curl wget unzip make
bash ps5/build.sh
```

Ergebnis: `ps5/dist/PPSA90410/` und `ps5/dist/PPSA90410.zip` (Titelordner oben im Archiv) sowie
`ps5/dist/SHA256SUMS`. Der Build verwendet ausschließlich öffentliche Werkzeuge, alle gepinnt:

| Schritt | Quelle | Prüfung |
| --- | --- | --- |
| Payload-SDK v0.42, `ps5-native-tool`, Clean-Room-`libc.prx` | [drakmor/ps5-native-app-boilerplate](https://github.com/drakmor/ps5-native-app-boilerplate) `470695e` | SDK-ZIP-SHA-256 im Boilerplate; `libc.prx`-SHA-256 `e6ff45d1…` = hardwaregetesteter Wert |
| Titel-Runtime (Heap, `opendir`, `realpath`, `exit`, Threads) | [lowbit/ps5-openrct2](https://github.com/lowbit/ps5-openrct2) `e2d9a82`, in `ps5/runtime/` | unverändert übernommen |
| SDL 2 | [ps5-payload-dev/SDL](https://github.com/ps5-payload-dev/SDL) `ee4c47dc` + `ps5/patches/sdl` | Archiv-SHA-256 oder Git-Commit-Hash |
| zlib 1.3.2 (nur für das Host-Werkzeug) | zlib.net oder GitHub-Spiegel | SHA-256 `bb329a0a…` |

`eboot.bin` und `libc.prx` werden mit `ps5-native-tool self --inspect` geprüft („integrity: valid").
Systemmodule, die ein Spielprozess nicht lädt (`libScePosixForWebKit`, `libkernel_sys`,
`libkernel_web`), sind vom Linken ausgeschlossen: Ein solcher Import bricht den Build ab, statt zur
Laufzeit ins Leere zu springen.

Unter Windows:

```powershell
.\ps5\build-wsl.ps1 -Distribution Ubuntu-24.04 -Checkout /mnt/c/repos/Akeno-Store -Setup
```

Ohne `-Setup` baut der Aufruf nur. GitHub Actions (`.github/workflows/build.yml`) führt beide
Testsuites aus und veröffentlicht `PPSA90410.zip` als Artefakt jedes Pushes.

## Native App auf dem Host

```sh
sudo apt install libsdl2-dev libcurl4-openssl-dev
make -C native test        # 333 Prüfungen des Kerns
make -C native app         # native/build/host/akeno
make -C native probe       # Live-Abfrage aller Stores
make native-screenshots    # Screenshot-Durchlauf aller Ansichten (z. B. unter Xvfb)
```

`native/build/host/akeno-probe install:<TITLE-ID>` lädt einen Katalogtitel real herunter, prüft und
installiert ihn in den simulierten Ordner.

## Python-Host-Hub

```sh
make test     # native-test + Python-Tests
make build    # dist/akeno-gaming-hub-host.zip (Quellpaket)
AKENO_LIVE_STORES=prosperostore,itch python3 -m unittest discover -s tests -p test_stores_live.py -v
```
