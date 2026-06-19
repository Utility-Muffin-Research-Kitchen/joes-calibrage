# Joe's Calibrage MLP1 cross-build. Runs inside the mlp1-toolchain container with
# the UMRK workspace mounted at /workspace (joes-calibrage, Catastrophe as siblings).
TC       := /opt/mlp1-toolchain
CC       := $(TC)/bin/aarch64-buildroot-linux-gnu-gcc
SYSROOT  := $(TC)/aarch64-buildroot-linux-gnu/sysroot
APP      ?= /workspace/joes-calibrage
CATA     ?= /workspace/Catastrophe
# Catastrophe's stylesheet loader needs cJSON (sourced from the Jawaka sibling).
CJSON    ?= /workspace/Jawaka/third_party/cjson
OUT      := $(APP)/ports/mlp1/pak/bin/joes-calibrage
SRC      := $(wildcard $(APP)/src/*.c)

CFLAGS  := -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -DPLATFORM_MLP1 \
           -I$(APP)/src -I$(CATA)/include -I$(CJSON) \
           -I$(SYSROOT)/usr/include -I$(SYSROOT)/usr/include/SDL2
LDFLAGS := -L$(SYSROOT)/usr/lib -lSDL2 -lSDL2_ttf -lSDL2_image -lGLESv2 -lEGL -lm -lpthread

.PHONY: all clean
all:
	mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) -o $(OUT) $(SRC) $(CJSON)/cJSON.c $(LDFLAGS)
clean:
	rm -f $(OUT)
