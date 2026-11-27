PYTHON ?= python3

.PHONY: run test build
run:
	$(PYTHON) -m core.server

test:
	$(PYTHON) -m unittest discover -s tests -v

build: test
	$(PYTHON) build/package_host.py

NATIVE_SOURCES := apps/ps5-hub/native/main.c apps/ps5-hub/native/catalog.c
NATIVE_HEADERS := $(wildcard apps/ps5-hub/native/*.h)
NATIVE_WARNINGS := -std=c11 -Wall -Wextra -Wpedantic
SDL2_CFLAGS ?= $(shell pkg-config --cflags sdl2)
SDL2_LIBS ?= $(shell pkg-config --libs sdl2)

.PHONY: native native-test native-catalog ps5
native: dist/native/akeno-hub

dist/native/akeno-hub: $(NATIVE_SOURCES) $(NATIVE_HEADERS) Makefile
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(NATIVE_WARNINGS) $(SDL2_CFLAGS) $(NATIVE_SOURCES) $(LDFLAGS) $(SDL2_LIBS) -o $@

native-test: native
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy dist/native/akeno-hub --self-test

native-catalog:
	$(PYTHON) -m core.native_catalog $(CATALOG_ARGS)

ps5: $(NATIVE_SOURCES) $(NATIVE_HEADERS)
	@test -n "$(PS5_CC)" && test -n "$(PS5_SDL2_CFLAGS)" && test -n "$(PS5_SDL2_LIBS)" || { printf '%s\n' 'PS5_CC, PS5_SDL2_CFLAGS und PS5_SDL2_LIBS für das autorisierte Ziel-SDK angeben.' >&2; exit 1; }
	mkdir -p dist/ps5
	$(PS5_CC) $(PS5_APP_CPPFLAGS) $(PS5_CPPFLAGS) $(PS5_CFLAGS) $(NATIVE_WARNINGS) $(PS5_SDL2_CFLAGS) $(NATIVE_SOURCES) $(PS5_LDFLAGS) $(PS5_SDL2_LIBS) -o dist/ps5/akeno-hub.elf

PS5_APP_CPPFLAGS := -DAKENO_APP_DATA_DIR='"/app0/assets"'
PS5_CLANG ?= clang
PS5_OBJECTS := $(patsubst apps/ps5-hub/native/%.c,dist/ps5/obj/%.o,$(NATIVE_SOURCES))

.PHONY: ps5-objects
ps5-objects:
	@test -n "$(PS5_SYSROOT)" && test -f "$(PS5_SYSROOT)/include/sys/types.h" && test -f "$(PS5_SYSROOT)/user/homebrew/include/SDL2/SDL.h" || { printf '%s\n' 'PS5_SYSROOT muss auf den Ziel-Headerbaum mit SDL2 zeigen.' >&2; exit 1; }
	mkdir -p dist/ps5/obj
	$(PS5_CLANG) $(PS5_APP_CPPFLAGS) -fPIC --target=x86_64-unknown-freebsd --sysroot="$(PS5_SYSROOT)" -isystem "$(PS5_SYSROOT)/include" -I"$(PS5_SYSROOT)/user/homebrew/include/SDL2" $(NATIVE_WARNINGS) -Werror -O2 -c apps/ps5-hub/native/main.c -o $(word 1,$(PS5_OBJECTS))
	$(PS5_CLANG) $(PS5_APP_CPPFLAGS) -fPIC --target=x86_64-unknown-freebsd --sysroot="$(PS5_SYSROOT)" -isystem "$(PS5_SYSROOT)/include" -I"$(PS5_SYSROOT)/user/homebrew/include/SDL2" $(NATIVE_WARNINGS) -Werror -O2 -c apps/ps5-hub/native/catalog.c -o $(word 2,$(PS5_OBJECTS))

PS5_LLD ?= ld.lld
PS5_PORTS = $(PS5_SYSROOT)/user/homebrew/lib
PS5_MODULE_LIBS := -lSDL2 -liconv -lsamplerate -lSceSystemService -lSceUserService \
	-lScePad -lSceAudioOut -lSceVideoOut -lSceKeyboard -lSceImeDialog -lSceLibcInternal -lkernel -lc

.PHONY: ps5-module
ps5-module: ps5-objects
	$(PS5_LLD) -shared --no-undefined --exclude-libs ALL -soname akeno-ui.so \
		-o dist/ps5/akeno-ui.so $(PS5_OBJECTS) -L"$(PS5_SYSROOT)/lib" -L"$(PS5_PORTS)" $(PS5_MODULE_LIBS)
