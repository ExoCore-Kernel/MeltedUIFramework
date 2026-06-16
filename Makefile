CC ?= cc
SDL2_CONFIG ?= sdl2-config
PKG_CONFIG ?= pkg-config

ifeq ($(origin MELTED_GLASS_DIR), undefined)
MELTED_GLASS_DIR := external/MeltedGlassOpenGL
ifeq ($(wildcard $(MELTED_GLASS_DIR)/include/melted_glass.h),)
MELTED_GLASS_DIR := ../MeltedGlassOpenGL
ifeq ($(wildcard $(MELTED_GLASS_DIR)/include/melted_glass.h),)
MELTED_GLASS_DIR := ../LiquidGlassOpenGL
endif
endif
endif

LIB := build/libmelted_ui.a
LIB_OBJ := build/melted_ui.o
SWITCH_DEMO := build/melted_ui_switch_demo
DEMOS := $(SWITCH_DEMO)

COMMON_CFLAGS := -std=c11 -Wall -Wextra -O2 -Iinclude -I$(MELTED_GLASS_DIR)/include
DEMO_CFLAGS := $(COMMON_CFLAGS) -DMELTED_GLASS_CONFIG_PATH='"$(MELTED_GLASS_DIR)/config/melted_glass_config.json"' $(shell $(SDL2_CONFIG) --cflags) $(shell $(PKG_CONFIG) --cflags libpng)
DEMO_LDFLAGS := $(shell $(SDL2_CONFIG) --libs) $(shell $(PKG_CONFIG) --libs libpng) -framework OpenGL

.PHONY: all lib demo run run-switch screenshot smoke smoke-switch clean

all: lib demo

lib: $(LIB)

demo: $(DEMOS)

$(MELTED_GLASS_DIR)/build/libmelted_glass.a:
	$(MAKE) -C $(MELTED_GLASS_DIR) lib

$(LIB_OBJ): src/melted_ui.c include/melted_ui.h $(MELTED_GLASS_DIR)/include/melted_glass.h
	mkdir -p build
	$(CC) $(COMMON_CFLAGS) -c src/melted_ui.c -o $@

$(LIB): $(LIB_OBJ)
	ar rcs $@ $<

$(SWITCH_DEMO): examples/switch_demo.c $(LIB) $(MELTED_GLASS_DIR)/build/libmelted_glass.a
	mkdir -p build
	$(CC) $(DEMO_CFLAGS) examples/switch_demo.c $(LIB) $(MELTED_GLASS_DIR)/build/libmelted_glass.a -o $@ $(DEMO_LDFLAGS)

run: run-switch

run-switch: $(SWITCH_DEMO)
	./$(SWITCH_DEMO)

screenshot: $(SWITCH_DEMO)
	mkdir -p docs/images
	./$(SWITCH_DEMO) --screenshot docs/images/melted-ui-controls.png --frames 4

smoke: smoke-switch

smoke-switch: $(SWITCH_DEMO)
	./$(SWITCH_DEMO) --hidden --frames 2

clean:
	rm -rf build
