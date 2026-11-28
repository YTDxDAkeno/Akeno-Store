# Akeno Gaming Hub - native app, Linux host build (the PS5 build is ps5/build.sh).
# SPDX-License-Identifier: GPL-3.0-or-later

CXX ?= clang++
CC ?= clang
BUILD ?= build/host
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS := $(shell pkg-config --libs sdl2)
CURL_LIBS := $(shell pkg-config --libs libcurl)
WARN := -Wall -Wextra -Wno-missing-field-initializers
INCLUDES := -Isrc -Ithird_party/yyjson -Ithird_party/monocypher -Ithird_party/miniz -Ithird_party/picosha2 \
	-Ithird_party/stb -Ithird_party/qrcodegen
CXXFLAGS ?= -O2 -g
CFLAGS ?= -O2 -g
# The integrated RPG Maker 2000/2003 runtime (EasyRPG Player), built by tools/easyrpg.sh.
# EASYRPG=0 builds the hub without it.
EASYRPG ?= 1
EASYRPG_LIB := build/easyrpg/host/libakeno-easyrpg.a
ifeq ($(EASYRPG),1)
RUNTIME_FLAGS := -DAKENO_WITH_EASYRPG=1
RUNTIME_LIBS := $(EASYRPG_LIB) $(shell pkg-config --libs pixman-1 libpng zlib)
endif
ALL_CXXFLAGS := -std=c++20 -fno-exceptions -fno-rtti $(WARN) $(INCLUDES) $(SDL_CFLAGS) -DAKENO_HOST=1 -DAKENO_ASSET_DIR='"$(CURDIR)/assets"' $(RUNTIME_FLAGS) $(CXXFLAGS)
ALL_CFLAGS := -std=c11 $(INCLUDES) -DMINIZ_NO_TIME -D_DEFAULT_SOURCE $(CFLAGS)

CORE_SOURCES := $(wildcard src/core/*.cpp src/net/*.cpp src/stores/*.cpp src/install/*.cpp src/runtime/*.cpp) \
	src/play/library.cpp src/play/arguments.cpp src/platform/host.cpp src/platform/host_http.cpp
UI_SOURCES := $(wildcard src/ui/*.cpp) src/main.cpp src/play/play.cpp src/play/easyrpg_backend.cpp
THIRD_PARTY := third_party/yyjson/yyjson.c third_party/monocypher/monocypher.c \
	third_party/monocypher/monocypher-ed25519.c third_party/miniz/miniz.c third_party/miniz/miniz_tdef.c \
	third_party/miniz/miniz_tinfl.c third_party/miniz/miniz_zip.c third_party/qrcodegen/qrcodegen.c

obj = $(patsubst %,$(BUILD)/obj/%.o,$(1))
CORE_OBJECTS := $(call obj,$(CORE_SOURCES)) $(call obj,$(THIRD_PARTY))
UI_OBJECTS := $(call obj,$(UI_SOURCES))

.PHONY: all test app clean screenshots
all: app

app: $(BUILD)/akeno

$(BUILD)/akeno: $(CORE_OBJECTS) $(UI_OBJECTS) $(if $(RUNTIME_LIBS),$(EASYRPG_LIB))
	$(CXX) $(CORE_OBJECTS) $(UI_OBJECTS) $(RUNTIME_LIBS) $(SDL_LIBS) $(CURL_LIBS) -pthread -o $@

$(EASYRPG_LIB): tools/easyrpg.sh src/play/easyrpg/akeno_easyrpg.cpp
	bash tools/easyrpg.sh host

$(BUILD)/akeno-tests: $(CORE_OBJECTS) $(call obj,tests/core_tests.cpp)
	$(CXX) $^ $(CURL_LIBS) -pthread -o $@

test: $(BUILD)/akeno-tests
	$(BUILD)/akeno-tests

$(BUILD)/obj/tests/core_tests.cpp.o: ALL_CXXFLAGS += -DAKENO_FIXTURES='"$(CURDIR)/tests/fixtures"'

$(BUILD)/obj/%.cpp.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(ALL_CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/obj/%.c.o: %.c
	@mkdir -p $(@D)
	$(CC) $(ALL_CFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

$(BUILD)/akeno-probe: $(CORE_OBJECTS) $(call obj,tests/live_probe.cpp)
	$(CXX) $^ $(CURL_LIBS) -pthread -o $@

probe: $(BUILD)/akeno-probe
	$(BUILD)/akeno-probe $(PROBE_TITLES)
