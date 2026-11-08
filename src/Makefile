CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O2

.PHONY: all test clean
all: gameboy test_cpu test_bus test_timer test_ppu test_mbc3 test_apu

gameboy: main.c cpu.c bus.c cartridge.c timer.c ppu.c apu.c cpu.h bus.h cartridge.h timer.h ppu.h apu.h
	$(CC) $(CFLAGS) main.c cpu.c bus.c cartridge.c timer.c ppu.c apu.c -o $@

test_cpu: cpu.c cpu.h test_cpu.c
	$(CC) $(CFLAGS) cpu.c test_cpu.c -o $@

test_bus: cpu.c bus.c cartridge.c timer.c ppu.c apu.c cpu.h bus.h cartridge.h timer.h ppu.h apu.h test_bus.c
	$(CC) $(CFLAGS) cpu.c bus.c cartridge.c timer.c ppu.c apu.c test_bus.c -o $@

test_timer: cpu.c bus.c cartridge.c timer.c ppu.c apu.c cpu.h bus.h cartridge.h timer.h ppu.h apu.h test_timer.c
	$(CC) $(CFLAGS) cpu.c bus.c cartridge.c timer.c ppu.c apu.c test_timer.c -o $@

test_ppu: test_ppu.c bus.c cartridge.c timer.c ppu.c apu.c bus.h ppu.h apu.h
	$(CC) $(CFLAGS) test_ppu.c bus.c cartridge.c timer.c ppu.c apu.c -o $@

test_mbc3: test_mbc3.c cartridge.c cartridge.h
	$(CC) $(CFLAGS) cartridge.c test_mbc3.c -o $@

test_apu: test_apu.c apu.c apu.h bus.c bus.h cartridge.c timer.c ppu.c
	$(CC) $(CFLAGS) test_apu.c apu.c bus.c cartridge.c timer.c ppu.c -o $@

test: test_cpu test_bus test_timer test_ppu test_mbc3 test_apu
	./test_cpu
	./test_bus
	./test_timer
	./test_ppu
	./test_mbc3
	./test_apu

clean:
	rm -f gameboy gameboy_sdl test_cpu test_bus test_timer test_ppu test_mbc3 test_apu

# Optional SDL2 graphical frontend. `make` and `make test` need no SDL2.
# Install SDL2 development headers and pkg-config before running `make gameboy_sdl`.
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)

.PHONY: sdl
sdl: gameboy_sdl

gameboy_sdl: frontend_sdl.c cpu.c bus.c cartridge.c timer.c ppu.c apu.c cpu.h bus.h cartridge.h timer.h ppu.h apu.h
	@if ! pkg-config --exists sdl2; then echo "SDL2 development package not found. Install libsdl2-dev (Linux) or mingw-w64 SDL2 (MSYS2)."; exit 1; fi
	$(CC) $(CFLAGS) $(SDL_CFLAGS) frontend_sdl.c cpu.c bus.c cartridge.c timer.c ppu.c apu.c -o $@ $(SDL_LIBS)
