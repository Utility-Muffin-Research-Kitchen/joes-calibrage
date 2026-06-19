SHELL := /bin/bash

# Joe's Calibrage — UMRK fork, Catastrophe UI, Miniloong Pocket 1 (Leaf) target.
# Leaf stages this app with `make package-platform PLATFORM=mlp1`, which builds
# the aarch64 binary in the mlp1-toolchain container (UMRK workspace mounted, so
# Catastrophe + Jawaka are siblings) and assembles the staged pak.

APP_NAME := joes-calibrage
PAK_NAME := Joe's Calibrage
BUILD_DIR := build
MLP1_BIN := ports/mlp1/pak/bin/joes-calibrage
MLP1_PACKAGE := $(BUILD_DIR)/mlp1/package/$(PAK_NAME).pak

TEST_BUILD_DIR := $(BUILD_DIR)/tests
TEST_BIN := $(TEST_BUILD_DIR)/calibrage_tests
TEST_SRC := tests/calibrage_tests.c src/calibration.c src/config.c src/platform.c src/raw_input.c

# Desktop dev build resolves the toolkit + cJSON from workspace siblings.
CATASTROPHE_DIR ?= ../Catastrophe
CJSON_DIR ?= ../Jawaka/third_party/cjson

.PHONY: all mlp1 package-platform package-mlp1 test-native mac run-mac clean help

all: mlp1

# Native unit tests — the calibration engine is toolkit-agnostic (no Catastrophe).
$(TEST_BIN): $(TEST_SRC)
	@mkdir -p $(TEST_BUILD_DIR)
	cc -std=gnu11 -O0 -g -DPLATFORM_MAC -DTESTING -Isrc -o $(TEST_BIN) $(TEST_SRC)

test-native: $(TEST_BIN)
	./$(TEST_BIN)

# Cross-compile the aarch64 binary (Docker mlp1-toolchain).
mlp1:
	@./scripts/build-mlp1.sh

package-platform:
	@test -n "$(PLATFORM)" || { echo "usage: make package-platform PLATFORM=mlp1" >&2; exit 1; }
	@case "$(PLATFORM)" in \
		mlp1) $(MAKE) package-mlp1 ;; \
		*) echo "unsupported package platform: $(PLATFORM)" >&2; exit 1 ;; \
	esac

# Build, then assemble the staged pak: launch.sh + pak.json + the built binary.
package-mlp1: mlp1
	@rm -rf "$(MLP1_PACKAGE)"
	@mkdir -p "$(MLP1_PACKAGE)/bin"
	@cp launch.sh pak.json "$(MLP1_PACKAGE)/"
	@if [ -f LICENSE ]; then cp LICENSE "$(MLP1_PACKAGE)/"; fi
	@cp "$(MLP1_BIN)" "$(MLP1_PACKAGE)/bin/$(APP_NAME)"
	@echo "=== Packaged: $(MLP1_PACKAGE) ==="

# Desktop dev build against the sibling Catastrophe (best-effort; needs brew SDL2).
mac:
	@mkdir -p $(BUILD_DIR)/mac
	cc -std=gnu11 -O0 -g -Isrc -I$(CATASTROPHE_DIR)/include -I$(CJSON_DIR) \
		$(shell pkg-config --cflags sdl2 SDL2_ttf SDL2_image) \
		-o $(BUILD_DIR)/mac/$(APP_NAME) \
		$(wildcard src/*.c) $(CJSON_DIR)/cJSON.c \
		$(shell pkg-config --libs sdl2 SDL2_ttf SDL2_image) -lm -lpthread

run-mac: mac
	./$(BUILD_DIR)/mac/$(APP_NAME)

clean:
	rm -rf $(BUILD_DIR) ports/mlp1/pak/bin

help:
	@echo "make test-native      run the engine unit tests"
	@echo "make mlp1             cross-build the aarch64 binary (Docker mlp1-toolchain)"
	@echo "make package-mlp1     build + assemble the .pak under build/mlp1/package"
	@echo "make mac             desktop dev build (sibling Catastrophe + brew SDL2)"
