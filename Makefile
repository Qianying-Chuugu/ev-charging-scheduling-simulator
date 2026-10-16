# Makefile 描述项目的构建规则和依赖关系，统一的编译入口/编译配置文件

CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -pedantic -Isrc

BACKEND_SRC = \
	src/module1_graph/graph.c \
	src/module2_scheduler/scheduler.c \
	src/module3_entity/entity.c \
	src/module4_charger/charger.c \
	src/module5_recommend/recommend.c \
	src/module6_simulation/simulation.c \
	src/module7_fileio/fileio.c

GTK_UI_SRC = \
	src/module8_ui/gtk4_ui.c \
	src/module9_timer/timer.c

.PHONY: all gui gtk4 test clean

PKG_CONFIG ?= pkg-config
GTK4_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk4 2>/dev/null)
GTK4_LIBS := $(shell $(PKG_CONFIG) --libs gtk4 2>/dev/null)

all: build/charging_system_gtk4.exe build/backend_demo.exe

build:
	mkdir -p build

gui: build/charging_system_gtk4.exe

gtk4: build/charging_system_gtk4.exe

build/backend_demo.exe: src/app/main.c $(BACKEND_SRC) | build
	$(CC) $(CFLAGS) $^ -o $@

build/charging_system_gtk4.exe: $(GTK_UI_SRC) $(BACKEND_SRC) | build
	@$(PKG_CONFIG) --exists gtk4 || (echo "GTK 4 development files not found; install GTK 4 and pkg-config first." && exit 1)
	$(CC) $(CFLAGS) $(GTK4_CFLAGS) $^ $(GTK4_LIBS) -lm -o $@

build/test_graph.exe: tests/module1_graph/test_graph.c src/module1_graph/graph.c | build
	$(CC) $(CFLAGS) $^ -o $@

build/test_scheduler.exe: tests/module2_scheduler/test_scheduler.c src/module2_scheduler/scheduler.c | build
	$(CC) $(CFLAGS) $^ -o $@

build/test_entity.exe: tests/module3_entity/test_entity.c src/module1_graph/graph.c src/module3_entity/entity.c | build
	$(CC) $(CFLAGS) $^ -o $@

build/test_charger.exe: tests/module4_charger/test_charger.c src/module1_graph/graph.c src/module3_entity/entity.c src/module4_charger/charger.c | build
	$(CC) $(CFLAGS) $^ -o $@

build/test_recommend.exe: tests/module5_recommend/test_recommend.c src/module1_graph/graph.c src/module2_scheduler/scheduler.c src/module3_entity/entity.c src/module4_charger/charger.c src/module5_recommend/recommend.c | build
	$(CC) $(CFLAGS) $^ -o $@

build/test_simulation.exe: tests/module6_simulation/test_simulation.c src/module1_graph/graph.c src/module2_scheduler/scheduler.c src/module3_entity/entity.c src/module4_charger/charger.c src/module5_recommend/recommend.c src/module6_simulation/simulation.c | build
	$(CC) $(CFLAGS) $^ -o $@

build/test_fileio.exe: tests/module7_fileio/test_fileio.c src/module1_graph/graph.c src/module3_entity/entity.c src/module4_charger/charger.c src/module7_fileio/fileio.c | build
	$(CC) $(CFLAGS) $^ -o $@

build/test_backend.exe: tests/integration/test_backend.c $(BACKEND_SRC) | build
	$(CC) $(CFLAGS) $^ -o $@

build/test_timer.exe: tests/module9_timer/test_timer.c src/module9_timer/timer.c | build
	@$(PKG_CONFIG) --exists gtk4 || (echo "GTK 4 development files not found; install GTK 4 and pkg-config first." && exit 1)
	$(CC) $(CFLAGS) $(GTK4_CFLAGS) $^ $(GTK4_LIBS) -o $@

test: build/test_graph.exe build/test_scheduler.exe build/test_entity.exe build/test_charger.exe build/test_recommend.exe build/test_simulation.exe build/test_fileio.exe build/test_backend.exe build/test_timer.exe
	./build/test_graph.exe
	./build/test_scheduler.exe
	./build/test_entity.exe
	./build/test_charger.exe
	./build/test_recommend.exe
	./build/test_simulation.exe
	./build/test_fileio.exe
	./build/test_backend.exe
	./build/test_timer.exe

clean:
	$(RM) build/backend_demo.exe build/charging_system_gtk4.exe build/test_graph.exe build/test_scheduler.exe build/test_entity.exe build/test_charger.exe build/test_recommend.exe build/test_simulation.exe build/test_fileio.exe build/test_backend.exe build/test_timer.exe

