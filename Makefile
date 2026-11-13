# Bully Studio - convenience wrapper around CMake.
#
# The Makefile is deliberately thin: every target is a one-liner over CMake, so
# there is exactly one source of truth for the build (CMakeLists.txt +
# CMakePresets.json). Use CMake directly in CI; use `make` interactively.
#
#   make               list targets
#   make setup         install dependencies (Arch Linux)
#   make configure     configure ./build (Debug, full build)
#   make build         incremental build
#   make test          fast test suite
#   make test-all      test suite + QA over examples/
#   make editor        build and launch the Studio GUI
#   make run           build and launch the example game
#   make package       package the example project
#   make clean         remove ./build

SHELL := /bin/bash
.DEFAULT_GOAL := help

BUILD_DIR   ?= build
BUILD_TYPE  ?= Debug
JOBS        ?= $(shell nproc 2>/dev/null || echo 4)
CMAKE       ?= cmake
CTEST       ?= ctest
CLI         := $(BUILD_DIR)/apps/asset_cli/bully_asset_cli
EXAMPLE     ?= examples/hello-retro

# Presets: debug (default), headless, release, asan - see CMakePresets.json
PRESET      ?= debug

.PHONY: help setup configure build editor run test test-all package clean docs format check

help:
	@echo "Bully Studio build targets"
	@echo ""
	@echo "  make setup        install system dependencies (Arch Linux, pacman)"
	@echo "  make configure    configure $(BUILD_DIR)/ ($(BUILD_TYPE), preset: $(PRESET))"
	@echo "  make build        incremental build"
	@echo "  make editor       build and launch the Studio GUI"
	@echo "  make run          build and launch the example game"
	@echo "  make test         fast unit/integration suite"
	@echo "  make test-all     suite + QA over examples/ projects"
	@echo "  make package      build a .bpackage from $(EXAMPLE)"
	@echo "  make clean        remove $(BUILD_DIR)/"
	@echo "  make docs         print the documentation index"
	@echo ""
	@echo "Variables: BUILD_DIR=$(BUILD_DIR) BUILD_TYPE=$(BUILD_TYPE) JOBS=$(JOBS) PRESET=$(PRESET)"

setup:
	@command -v pacman >/dev/null || { echo "not Arch Linux: see docs/building.md"; exit 1; }
	sudo pacman -S --needed base-devel cmake ninja git sqlite catch2

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

# CMake is idempotent: configure if the cache is missing, then build.
build:
	@test -f $(BUILD_DIR)/CMakeCache.txt || $(CMAKE) -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	$(CMAKE) --build $(BUILD_DIR) -j$(JOBS)

editor: build
	@test -x $(BUILD_DIR)/apps/studio/bully_studio || { echo "Studio was not built (needs Qt 6: -DBULLY_BUILD_STUDIO=ON)"; exit 1; }
	$(BUILD_DIR)/apps/studio/bully_studio

run: build
	@test -x $(BUILD_DIR)/apps/game/bully_game || { echo "Game was not built (needs SDL3: -DBULLY_BUILD_RUNTIME=ON)"; exit 1; }
	$(BUILD_DIR)/apps/game/bully_game $(EXAMPLE)/project.bullyproj

test: build
	$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure

# test-all additionally runs the QA dashboard over every example project, so a
# content regression fails the build just like a code regression.
test-all: test
	scripts/qa_examples.sh $(CLI)

package: build
	@test -d $(EXAMPLE) || { echo "no example project at $(EXAMPLE)"; exit 1; }
	$(CLI) build --project $(EXAMPLE)
	$(CLI) package --project $(EXAMPLE)

clean:
	rm -rf $(BUILD_DIR)

docs:
	@cat docs/README.md
