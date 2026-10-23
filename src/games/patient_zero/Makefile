# PATIENT ZERO - build
#   make            optimised build (-O2)
#   make run        build and run
#   make debug      -O0 -g with AddressSanitizer + UBSan
#   make release    -O3, LTO, native tuning off (portable), stripped
#   make bundle     macOS .app bundle via scripts/bundle_macos.sh
#   make clean
#
# raylib is located with pkg-config, falling back to Homebrew prefixes
# (/opt/homebrew on Apple Silicon, /usr/local on Intel) or RAYLIB_DIR.

TARGET   := patient_zero
CC       ?= clang
SRC_DIR  := src
BUILD    := build
UNAME_S  := $(shell uname -s)

SRCS := $(shell find $(SRC_DIR) -name '*.c')
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

WARN     := -Wall -Wextra -Wshadow -Wno-unused-parameter -Wno-missing-field-initializers
OPT      ?= -O2
EXTRA    ?=
ALL_CFLAGS = $(OPT) -std=c11 $(WARN) -I$(SRC_DIR) -MMD -MP -D_DEFAULT_SOURCE $(EXTRA)

# --- raylib discovery -----------------------------------------------------
RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib 2>/dev/null)
RAYLIB_LIBS   := $(shell pkg-config --libs raylib 2>/dev/null)
ifeq ($(strip $(RAYLIB_LIBS)),)
  ifneq ($(RAYLIB_DIR),)
    RAYLIB_CFLAGS := -I$(RAYLIB_DIR)/include
    RAYLIB_LIBS   := -L$(RAYLIB_DIR)/lib -lraylib
  else ifneq ($(wildcard /opt/homebrew/include/raylib.h),)
    RAYLIB_CFLAGS := -I/opt/homebrew/include
    RAYLIB_LIBS   := -L/opt/homebrew/lib -lraylib
  else ifneq ($(wildcard /usr/local/include/raylib.h),)
    RAYLIB_CFLAGS := -I/usr/local/include
    RAYLIB_LIBS   := -L/usr/local/lib -lraylib
  else
    RAYLIB_LIBS   := -lraylib
  endif
endif
ALL_CFLAGS += $(RAYLIB_CFLAGS)

ifeq ($(UNAME_S),Darwin)
  PLATFORM_LIBS := -framework CoreVideo -framework IOKit -framework Cocoa -framework GLUT -framework OpenGL
else
  PLATFORM_LIBS := -lGL -lm -lpthread -ldl -lrt -lX11
endif
LDLIBS := $(RAYLIB_LIBS) $(PLATFORM_LIBS) -lm

.PHONY: all run debug release bundle clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(BUILD)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

debug:
	$(MAKE) clean
	$(MAKE) OPT="-O0 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined" LDFLAGS="-fsanitize=address,undefined"

release:
	$(MAKE) clean
	$(MAKE) OPT="-O3 -flto -DNDEBUG" LDFLAGS="-flto"
	strip $(TARGET) 2>/dev/null || true

bundle: release
	sh scripts/bundle_macos.sh $(TARGET)

clean:
	rm -rf $(BUILD) $(TARGET) PatientZero.app

-include $(DEPS)
