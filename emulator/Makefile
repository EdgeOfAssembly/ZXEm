# ZXEm — debug by default (gnu-make + modern-c-cpp-quality).
CXX ?= g++
CXX := $(CXX) -Wl,--as-needed -Wl,-O1 -Wl,--hash-style=gnu
MAKEFLAGS += --no-print-directory

PKG_CONFIG ?= pkg-config
SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LIBS   := $(shell $(PKG_CONFIG) --libs sdl2)
ZIP_CFLAGS := $(shell $(PKG_CONFIG) --cflags libzip)
ZIP_LIBS   := $(shell $(PKG_CONFIG) --libs libzip)
Z_CFLAGS   := $(shell $(PKG_CONFIG) --cflags zlib)
Z_LIBS     := $(shell $(PKG_CONFIG) --libs zlib)

CXXFLAGS_COMMON := -std=gnu++23 -Wall -Wextra -Wpedantic -Wshadow \
	-fno-omit-frame-pointer -ffunction-sections -fdata-sections \
	$(SDL_CFLAGS) $(ZIP_CFLAGS) $(Z_CFLAGS) -I src

LDFLAGS_COMMON := $(SDL_LIBS) $(ZIP_LIBS) $(Z_LIBS) -lm -Wl,--gc-sections

CXXFLAGS_DEBUG := $(CXXFLAGS_COMMON) -g3 -O0 \
	-fsanitize=address,undefined -fno-omit-frame-pointer -rdynamic
LDFLAGS_DEBUG := -fsanitize=address,undefined -rdynamic $(LDFLAGS_COMMON)

CXXFLAGS_OPTIMIZED := -O3 -march=x86-64 -mtune=generic -fno-omit-frame-pointer
CXXFLAGS_RELEASE := $(CXXFLAGS_COMMON) -DNDEBUG $(CXXFLAGS_OPTIMIZED)
LDFLAGS_RELEASE := $(LDFLAGS_COMMON)

CXXFLAGS ?= $(CXXFLAGS_DEBUG)
LDFLAGS  ?= $(LDFLAGS_DEBUG)

SRCDIR := src
OBJDIR := obj
TARGET := zxem

LIB_SRCS := $(SRCDIR)/ay.cpp $(SRCDIR)/config.cpp $(SRCDIR)/log.cpp \
	$(SRCDIR)/media.cpp $(SRCDIR)/snapshot.cpp $(SRCDIR)/ula.cpp \
	$(SRCDIR)/vfs.cpp $(SRCDIR)/z80.cpp
MAIN_SRCS := $(SRCDIR)/main.cpp
SRCS := $(LIB_SRCS) $(MAIN_SRCS)
OBJS := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SRCS))
LIB_OBJS := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(LIB_SRCS))

BUILD_FLAGS := -s V=0 -j$(shell nproc 2>/dev/null || echo 1)

export PKG_CONFIG_PATH ?= $(HOME)/.local/share/pkgconfig:$(HOME)/.local/lib64/pkgconfig:$(HOME)/.local/lib/pkgconfig:$(PKG_CONFIG_PATH)

.PHONY: all clean test tests verify release profile

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET) tests/run_tests

# --- tests (Catch2) + CLI contracts (pytest) ---
TEST_BIN := tests/run_tests
TEST_SRCS := tests/test_cursor.cpp tests/test_formats.cpp tests/test_vfs.cpp tests/test_ula_map.cpp
PY := $(shell if [ -x ../.venv/bin/python ]; then echo ../.venv/bin/python; \
	elif [ -x /mnt/python/bin/python ]; then echo /mnt/python/bin/python; \
	else echo python3; fi)

CATCH2_CFLAGS := $(shell pkg-config --cflags catch2-with-main)
CATCH2_LIBS   := -L$(HOME)/.local/lib64 -L$(HOME)/.local/lib $(shell pkg-config --libs catch2-with-main)

$(TEST_BIN): $(TEST_SRCS) $(LIB_OBJS) | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -I src $(CATCH2_CFLAGS) -o $@ $(TEST_SRCS) $(LIB_OBJS) $(CATCH2_LIBS) $(LDFLAGS)

test: $(TARGET) $(TEST_BIN)
	./$(TEST_BIN)
	$(PY) -m pytest -q --tb=short tests/test_cli.py
tests: test

verify: test
	@if command -v $(HOME)/.local/bin/cbmc >/dev/null 2>&1; then \
	  $(HOME)/.local/bin/cbmc formal/tap_block.c --bounds-check --pointer-check \
	    --unwind 8 --unwinding-assertions; \
	else \
	  echo "formal: cbmc not on PATH — skipped"; \
	fi

release:
	$(MAKE) $(BUILD_FLAGS) clean CXXFLAGS="$(CXXFLAGS_RELEASE)" \
		LDFLAGS="$(LDFLAGS_RELEASE)" all
	@echo "Release binary built: $(TARGET)"

profile:
	$(MAKE) $(BUILD_FLAGS) clean \
		CXXFLAGS="$(CXXFLAGS_COMMON) -DNDEBUG $(CXXFLAGS_OPTIMIZED) -g -pg -fno-inline" \
		LDFLAGS="-pg $(LDFLAGS_COMMON)" all
	@echo "Profile binary built: $(TARGET)"
