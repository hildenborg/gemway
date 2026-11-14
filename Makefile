# GEMWay - GEM resource designer for Wayland
#
#   make            build ./gemway
#   make test       run the self tests (no display needed)
#   make install    install to $(PREFIX)/bin (default /usr/local)
#   make clean

PREFIX   ?= /usr/local
CXX      ?= g++
PKGS     := wayland-client wayland-cursor xkbcommon libdecor-0

CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++20 -Wall -Wextra -Wno-unused-parameter -Isrc
CXXFLAGS += $(shell pkg-config --cflags $(PKGS))
LDLIBS   += $(shell pkg-config --libs $(PKGS))

SRC := $(wildcard src/*.cpp src/*/*.cpp)
OBJ := $(SRC:src/%.cpp=build/%.o)
BIN := gemway

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

build/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(OBJ:.o=.d)

test: $(BIN)
	./tests/run_tests.sh ./$(BIN)

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)
	install -Dm644 data/gemway.desktop $(DESTDIR)$(PREFIX)/share/applications/gemway.desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN) $(DESTDIR)$(PREFIX)/share/applications/gemway.desktop

clean:
	rm -rf build $(BIN)

.PHONY: all test install uninstall clean
