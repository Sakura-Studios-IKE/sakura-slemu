# sakura-slemu — portable Makefile (Linux / macOS / *BSD / MinGW)
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

CC     ?= cc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable
LDFLAGS ?= -lm

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
EXE := slemu

ifeq ($(OS),Windows_NT)
  EXE := slemu.exe
endif

.PHONY: all clean install uninstall test e2e

all: $(EXE)

$(EXE): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.c src/slemu.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(EXE)

install: $(EXE)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(EXE) $(DESTDIR)$(BINDIR)/$(EXE)

test: $(EXE)
	@sh tests/run_tests.sh ./$(EXE)

e2e: $(EXE)
	@sh tests/run_e2e.sh ./$(EXE)
