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
MLP1_BUILD_PROFILE ?= release
WORKSPACE_ROOT ?= $(abspath ..)
MLP1_FLAGS_MK ?= $(firstword $(wildcard /opt/mlp1-toolchain/umrk/mlp1-build-flags.mk $(WORKSPACE_ROOT)/mlp1-toolchain/flags/mlp1-build-flags.mk ../mlp1-toolchain/flags/mlp1-build-flags.mk))
ifneq ($(MLP1_FLAGS_MK),)
include $(MLP1_FLAGS_MK)
else
UMRK_MLP1_TARGET_SOC ?= rk3566
UMRK_MLP1_TARGET_CPU ?= cortex-a55
UMRK_MLP1_PROFILE_CFLAGS ?= -O2 -mcpu=cortex-a55 -mtune=cortex-a55 -ffunction-sections -fdata-sections -DNDEBUG
UMRK_MLP1_PROFILE_LDFLAGS ?= -Wl,--gc-sections
endif

TEST_BUILD_DIR := $(BUILD_DIR)/tests
TEST_BIN := $(TEST_BUILD_DIR)/calibrage_tests
TEST_SRC := tests/calibrage_tests.c src/calibration.c src/config.c src/platform.c src/raw_input.c src/i18n.c

# Translation authoring/validation lane (mirrors Thing-File's i18n toolchain):
# i18n/*.po is the source of truth; release tables are the generated TSVs
# under build/i18n that get staged into the pak's res/i18n.
I18N_POS := $(wildcard i18n/*.po)
I18N_TSV_DIR := $(BUILD_DIR)/i18n
I18N_TSV := $(patsubst i18n/%.po,$(I18N_TSV_DIR)/%.tsv,$(I18N_POS))
I18N_COVERAGE_GATE ?= 90

# Desktop dev build resolves the toolkit + cJSON from workspace siblings.
CATASTROPHE_DIR ?= ../Catastrophe
CJSON_DIR ?= ../Jawaka/third_party/cjson

.PHONY: all mlp1 package-platform package-mlp1 test-native mac run-mac clean help \
	i18n-pot i18n-parser-test i18n-runtime-test i18n-check i18n-build i18n-review

all: mlp1

# Native unit tests — the calibration engine is toolkit-agnostic (no Catastrophe).
$(TEST_BIN): $(TEST_SRC)
	@mkdir -p $(TEST_BUILD_DIR)
	cc -std=gnu11 -O0 -g -DPLATFORM_MAC -DTESTING -Isrc -o $(TEST_BIN) $(TEST_SRC)

test-native: $(TEST_BIN)
	./$(TEST_BIN)

# Regenerate the canonical key list from direct literal T() calls in the
# sources. Commit the result -- `make i18n-check` diffs it, so a UI-string
# change without a regenerated .pot fails there.
i18n-pot:
	python3 tools/i18n-extract.py

i18n-parser-test:
	python3 tools/i18n-parser-test.py

$(TEST_BUILD_DIR)/i18n-runtime-test: tests/i18n-runtime-test.c src/i18n.c src/i18n.h
	@mkdir -p $(TEST_BUILD_DIR)
	cc -std=gnu11 -O0 -g -Isrc -o $@ tests/i18n-runtime-test.c src/i18n.c

i18n-runtime-test: $(TEST_BUILD_DIR)/i18n-runtime-test
	./$(TEST_BUILD_DIR)/i18n-runtime-test

# What CI runs: the committed .pot must match the code, and any committed
# translation must parse, carry no orphan or duplicate keys, keep its printf
# conversions compatible with its keys, and reach the coverage gate.
i18n-check: i18n-parser-test i18n-runtime-test
	python3 tools/i18n-extract.py --check --gate $(I18N_COVERAGE_GATE) --po $(I18N_POS)

# Release tables: fuzzy entries never ship.
$(I18N_TSV_DIR):
	@mkdir -p $@

$(I18N_TSV_DIR)/%.tsv: i18n/%.po tools/i18n-po2tsv.py | $(I18N_TSV_DIR)
	python3 tools/i18n-po2tsv.py $< -o $@

i18n-build: $(I18N_TSV)

# Live-review tables (fuzzy entries included on purpose), named so they can be
# dropped into $USERDATA_PATH/joes-calibrage/i18n/ for an on-device wording pass.
i18n-review:
	@mkdir -p $(BUILD_DIR)/i18n-review
	@for po in $(I18N_POS); do \
		lang=$${po##*/}; lang=$${lang%.po}; \
		python3 tools/i18n-po2tsv.py --fuzzy "$$po" -o "$(BUILD_DIR)/i18n-review/$$lang.tsv" || exit 1; \
	done

# Cross-compile the aarch64 binary (Docker mlp1-toolchain).
mlp1:
	@MLP1_BUILD_PROFILE="$(MLP1_BUILD_PROFILE)" ./scripts/build-mlp1.sh

package-platform:
	@test -n "$(PLATFORM)" || { echo "usage: make package-platform PLATFORM=mlp1" >&2; exit 1; }
	@case "$(PLATFORM)" in \
		mlp1) $(MAKE) package-mlp1 ;; \
		*) echo "unsupported package platform: $(PLATFORM)" >&2; exit 1 ;; \
	esac

# Build, then assemble the staged pak: launch.sh + pak.json + the built binary.
package-mlp1: mlp1 i18n-build
	@rm -rf "$(MLP1_PACKAGE)"
	@mkdir -p "$(MLP1_PACKAGE)/bin"
	@cp launch.sh pak.json "$(MLP1_PACKAGE)/"
	@if [ -f LICENSE ]; then cp LICENSE "$(MLP1_PACKAGE)/"; fi
	@if [ -d res ]; then cp -R res "$(MLP1_PACKAGE)/"; fi
	@if [ -n "$(I18N_POS)" ]; then mkdir -p "$(MLP1_PACKAGE)/res/i18n"; cp -f $(I18N_TSV) "$(MLP1_PACKAGE)/res/i18n/"; fi
	@cp "$(MLP1_BIN)" "$(MLP1_PACKAGE)/bin/$(APP_NAME)"
	@{ \
		printf '{\n'; \
		printf '  "platform": "mlp1",\n'; \
		printf '  "target_soc": "%s",\n' "$(UMRK_MLP1_TARGET_SOC)"; \
		printf '  "target_cpu": "%s",\n' "$(UMRK_MLP1_TARGET_CPU)"; \
		printf '  "build_profile": "%s",\n' "$(MLP1_BUILD_PROFILE)"; \
		printf '  "cflags": "%s",\n' "$(UMRK_MLP1_PROFILE_CFLAGS)"; \
		printf '  "ldflags": "%s",\n' "$(UMRK_MLP1_PROFILE_LDFLAGS)"; \
		printf '  "binaries": ["bin/$(APP_NAME)"],\n'; \
		printf '  "exceptions": []\n'; \
		printf '}\n'; \
	} > "$(MLP1_PACKAGE)/build-manifest.json"
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
	@echo "make i18n-pot        regenerate i18n/joes-calibrage.pot from T() calls"
	@echo "make i18n-check      validate .pot + translations (parser/runtime/coverage)"
	@echo "make i18n-build      build release TSVs from i18n/*.po under build/i18n"
