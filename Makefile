CC = cc
CFLAGS = -O2 -g
WARN = -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
SOURCES = src/main.c src/assemble.c src/dwarf.c src/elf.c src/util.c

.PHONY: all test sanitize clean
all: chip8asm
chip8asm: $(SOURCES) src/chip8.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c17 $(SOURCES) $(LDFLAGS) -o $@
tests/unit: tests/unit.c src/dwarf.c src/util.c src/chip8.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c17 -Isrc tests/unit.c src/dwarf.c src/util.c $(LDFLAGS) -o $@
test: chip8asm tests/unit
	./tests/unit
	python3 tests/test.py
sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie" LDFLAGS="-fsanitize=address,undefined -no-pie" test
clean:
	rm -f chip8asm tests/unit
