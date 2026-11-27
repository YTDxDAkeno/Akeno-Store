PYTHON ?= python3

# Host development hub (Python): web UI, HTML5 player, local runtimes and catalog tools.
.PHONY: run test build
run:
	$(PYTHON) -m core.server

test: native-test
	$(PYTHON) -m unittest discover -s tests -v

build: test
	$(PYTHON) build/package_host.py

# Native app (C++/SDL2): the same code runs on the Linux host and as the PS5 title.
.PHONY: native native-test native-run native-screenshots ps5
native:
	$(MAKE) -C native app

native-test:
	$(MAKE) -C native test

native-run: native
	native/build/host/akeno

native-screenshots: native
	mkdir -p native/build/screenshots
	native/build/host/akeno --screenshots native/build/screenshots

# PS5 title folder ps5/dist/PPSA90410 and ps5/dist/PPSA90410.zip
ps5:
	bash ps5/build.sh
