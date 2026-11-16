# Wildlands — pure C voxel sandbox on raylib 6.2
#
#   make            release build (bin/wildlands)
#   make debug      debug build   (bin/wildlands_debug)
#   make run        build + run
#   make test       build + run unit tests
#   make raylib     (re)build vendored raylib
#   make mods       build the example mod
#   make assets     regenerate original assets (dev only, needs Python+PIL)
#   make clean      remove build outputs
#   make distclean  clean + remove vendored raylib build

CC        ?= cc
J         := $(shell nproc 2>/dev/null || echo 2)

RAYLIB_DIR  := third_party/raylib
RAYLIB_SRC  := $(RAYLIB_DIR)/src
SYSROOT     := third_party/sysroot
RAYLIB_A    := $(RAYLIB_SRC)/libraylib.a

BIN_DIR   := bin
BUILD     ?= release
OBJ_DIR   := obj/$(BUILD)

BIN       := $(BIN_DIR)/wildlands$(if $(filter-out release,$(BUILD)),_$(BUILD),)
TEST_BIN  := $(BIN_DIR)/unit_tests

CFLAGS_BASE := -std=c11 -D_POSIX_C_SOURCE=200809L \
               -Iinclude -I$(RAYLIB_SRC) \
               -Wall -Wextra -Wshadow -MMD -MP
CFLAGS_release := -O2 -DNDEBUG
CFLAGS_debug   := -O0 -g -DWL_DEBUG
CFLAGS_asan    := -O1 -g -DWL_DEBUG -fsanitize=address -fno-omit-frame-pointer

LDFLAGS := -L$(SYSROOT)/usr/lib/x86_64-linux-gnu
LDLIBS  := $(RAYLIB_A) -lGL -lX11 -lXrandr -lXi -lXinerama -lXcursor -lxkbcommon \
           -lm -lpthread -ldl -lrt

SRC := $(shell find src -name '*.c' 2>/dev/null | sort)
OBJ := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SRC))
DEP := $(OBJ:.o=.d)

MOD_SRC := mods/example_mod/example_mod.c
MOD_OUT := mods/example_mod/mod.so

.PHONY: all debug asan run test raylib mods assets clean distclean help

all: $(BIN)

debug:
	$(MAKE) BUILD=debug all

asan:
	$(MAKE) BUILD=asan all

help:
	@echo "Wildlands make targets:"
	@echo "  all        release build -> $(BIN)"
	@echo "  debug      debug build   -> $(BIN_DIR)/wildlands_debug"
	@echo "  asan       AddressSanitizer build -> $(BIN_DIR)/wildlands_asan"
	@echo "  run        build + run"
	@echo "  test       build + run unit tests"
	@echo "  raylib     (re)build vendored raylib"
	@echo "  mods       build the example mod -> $(MOD_OUT)"
	@echo "  assets     regenerate assets (dev only, Python + Pillow)"
	@echo "  clean      remove bin/ and obj/"
	@echo "  distclean  clean + remove raylib build artifacts"

$(BIN): $(OBJ) $(RAYLIB_A) | $(BIN_DIR)
	$(CC) $(CFLAGS_$(BUILD)) -o $@ $(OBJ) $(LDFLAGS) $(LDLIBS)
	@echo "== built $@ =="

$(OBJ_DIR)/%.o: src/%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) $(CFLAGS_$(BUILD)) -c $< -o $@

$(OBJ_DIR) $(BIN_DIR):
	mkdir -p $@

# --- tests (link everything except the game's main) ------------------------
TEST_OBJ := $(filter-out $(OBJ_DIR)/main.o,$(OBJ))

$(TEST_BIN): tests/unit_tests.c $(TEST_OBJ) $(RAYLIB_A) | $(BIN_DIR)
	$(CC) $(CFLAGS_BASE) $(CFLAGS_$(BUILD)) -o $@ $< $(TEST_OBJ) $(LDFLAGS) $(LDLIBS)

test: $(TEST_BIN)
	./$(TEST_BIN)

# --- vendored raylib --------------------------------------------------------
$(RAYLIB_A):
	$(MAKE) -C $(RAYLIB_SRC) PLATFORM=PLATFORM_DESKTOP RAYLIB_LIBTYPE=STATIC \
		-j$(J) EXTRA_INCLUDE_PATHS="-I$(realpath $(SYSROOT))/usr/include"
	@echo "== raylib built =="

raylib: $(RAYLIB_A)

# --- example mod ------------------------------------------------------------
$(MOD_OUT): $(MOD_SRC) include/wl/mod_api.h
	$(CC) -std=c11 -fPIC -shared -Wall -Wextra -Iinclude -o $@ $(MOD_SRC)

mods: $(MOD_OUT)

# --- asset regeneration (development only) ---------------------------------
assets:
	python3 tools/gen_assets.py

clean:
	rm -rf bin obj

distclean: clean
	$(MAKE) -C $(RAYLIB_SRC) clean >/dev/null 2>&1 || true
	rm -f $(RAYLIB_A)

-include $(DEP)
