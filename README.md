# Game Boy Emulator — Built From Scratch in C

A work-in-progress emulator for the **original Nintendo Game Boy (DMG)**, implemented in **C11** with an **SDL2** frontend for graphics, keyboard input, and audio.

This project started as a CPU instruction interpreter and evolved into an emulator with cartridge loading, memory mapping, timers, interrupts, tile-based rendering, user input, MBC3 banking, save files, and four-channel audio. The goal is to learn how the original hardware works by building and testing each component separately.

> **Project status:** The CPU, memory bus, timer, PPU, cartridge controllers, and APU are implemented at varying levels of accuracy. Six automated core test suites passed in the development environment. **Successful gameplay of specific commercial ROMs has not yet been independently verified**, and the SDL2 executable still needs a local build/run check. Hardware timing and game compatibility remain works in progress.

## Table of contents

- [Features](#features)
- [Build and run](#build-and-run)
- [Controls](#controls)
- [Architecture](#architecture)
- [Development log](#development-log)
- [Issues encountered and solutions](#issues-encountered-and-solutions)
- [Screenshots — working ROMs](#screenshots--working-roms)
- [Testing](#testing)
- [Known limitations and next steps](#known-limitations-and-next-steps)
- [References](#references)

## Features

| Subsystem | Implementation |
| --- | --- |
| CPU | Sharp SM83 registers, base/CB-prefixed instructions, flags, stack, branching, interrupts, instruction-level T-cycle accounting |
| Memory bus | 64 KiB address map, VRAM, WRAM, echo RAM, OAM, HRAM, I/O dispatch, joypad, simplified OAM DMA |
| Cartridges | `.gb` ROM loader, ROM-only, MBC1, standard MBC3 (types `0x11`, `0x12`, `0x13`) |
| Save files | Automatic `.sav` load on startup and write on normal exit for battery-backed RAM |
| Timer | DIV/TIMA/TMA/TAC, selectable timer clocks, overflow/reload, timer interrupt requests |
| Graphics | 160 × 144 framebuffer, background/window tiles, 8 × 8 and 8 × 16 sprites, scrolling and DMG palettes |
| Display interrupts | VBlank and LCD STAT interrupt generation |
| Input | SDL2 keyboard mapping through the Game Boy P1 (`0xFF00`) joypad register |
| Audio | Four Game Boy channels (two pulse, wave, noise), APU registers, stereo mix, 48 kHz SDL2 output |
| Frontend | Resizable SDL2 window, pause, fast-forward, mute, and frame pacing (~59.73 FPS target) |

## Build and run

### Dependencies

- A C11 compiler and `make`.
- **SDL2** and `pkg-config` to build the graphical frontend. The core and its unit tests do not require SDL2.

**Ubuntu / Debian / WSL:**

```bash
sudo apt update
sudo apt install build-essential pkg-config libsdl2-dev
```

### Compile

Run these commands from the directory containing the `Makefile` (named `gameboy_cpu` in the distributed archive):

```bash
make                 # headless runner + core test binaries
make test            # run all six core test suites
make gameboy_sdl     # graphical / interactive frontend
```

### Play a ROM

```bash
./gameboy_sdl path/to/game.gb
./gameboy_sdl path/to/game.gb 5   # optional integer window scale, 1–10
```

On Windows under MSYS2, launch `./gameboy_sdl.exe` instead. Use a ROM that you are authorized to use; **ROM files are not included in this repository**.

To execute a limited number of CPU instructions without opening a window:

```bash
./gameboy path/to/game.gb 100000
```

The headless runner can also output an image in **PPM** format:

```bash
./gameboy path/to/game.gb 2000000 frame.ppm
```

> **WSL note:** SDL2 needs a working graphical/audio display environment. If you are on Windows and SDL cannot open a window, run from an environment with SDL display support or use MSYS2 UCRT64.

## Controls

| Keyboard | Game Boy / emulator action |
| --- | --- |
| Arrow keys | D-pad |
| Z | A |
| X | B |
| Enter | Start |
| Backspace | Select |
| Space | Pause / resume |
| Hold Tab | Fast-forward (audio silenced) |
| M | Mute / unmute |
| Esc / close window | Exit and attempt to save battery-backed RAM |

SDL2 polls the keyboard and constructs an **active-high 8-bit button mask**. The memory bus translates this into the Game Boy's **active-low P1 register at `0xFF00`**, including selection of the direction and action-button groups. Selected button transitions can request a joypad interrupt.

## Architecture

```text
                              .gb cartridge image
                                      |
                              Cartridge loader
                          ROM-only / MBC1 / MBC3
                                      |
                                      v
  Keyboard --> SDL2 frontend --> [ Memory bus ] <--> SM83 CPU
                 |                    |                |
                 |          +---------+---------+      | T-cycles
                 |          |         |         |      |
                 |         Timer     PPU       APU <---+
                 |          |         |         |
                 |      interrupts   |    audio samples
                 |                    |         |
                 +<---- framebuffer --+         +--> SDL2 audio
                 |
               SDL2 window
```

The CPU executes instructions and returns the consumed **T-cycles**. Each iteration advances the bus peripherals:

```c
unsigned cycles = gb_cpu_step(&cpu);
if (cycles == 0 || cpu.stopped) {
    /* Stop or report an unsupported/invalid operation. */
    break;
}
gb_bus_tick(&bus, cycles);  /* Advances timer, PPU, and APU. */
```

The CPU never needs to know about SDL2. It performs memory reads and writes using bus callbacks, while the frontend displays completed frames and plays sound samples. This separation makes individual subsystems easier to test.

### Source files

| File | Responsibility |
| --- | --- |
| `cpu.c`, `cpu.h` | Registers, instruction decoder/execution, flags, interrupts |
| `bus.c`, `bus.h` | Memory map, device routing, joypad, interrupt registers, DMA |
| `cartridge.c`, `cartridge.h` | ROM loading, cartridge banking, battery-backed RAM |
| `timer.c`, `timer.h` | Divider, programmable timer, overflow behavior |
| `ppu.c`, `ppu.h` | LCD modes, scanlines, background/window/sprite rendering |
| `apu.c`, `apu.h` | Four sound channels, audio register behavior, mixing |
| `frontend_sdl.c` | SDL2 window, keyboard, sound playback, frame pacing |
| `main.c` | Headless ROM execution and optional PPM frame output |
| `test_*.c` | Subsystem and integration tests |
| `Makefile` | Build targets and test runner |

## Development log

### Step 1 — Implemented the CPU

**Goal:** Execute Game Boy machine code without relying on a high-level simulation of game logic.

- Modelled eight 8-bit registers (`A`, `F`, `B`, `C`, `D`, `E`, `H`, `L`) plus 16-bit `PC` and `SP`.
- Implemented fetch–decode–execute for the Sharp SM83 instruction set, including CB-prefixed bit operations.
- Added arithmetic flags, stack operations, branches, calls, returns, and interrupt servicing.
- Began accounting for execution time in T-cycles rather than simply counting instructions.
- Verified basic programs that load registers, add values, and halt.

**What I learned:** How a real instruction set is represented as binary opcodes, and why program-counter updates, flag handling, and stack behavior must be precise.

### Step 2 — Added the memory bus and cartridge loader

**Goal:** Move beyond a flat memory array and load actual `.gb` cartridge images.

- Created a memory bus covering the Game Boy's 64 KiB address space.
- Routed ROM (`0x0000–0x7FFF`), VRAM, external cartridge RAM, WRAM, echo RAM, OAM, and I/O accesses to their appropriate components.
- Added ROM header parsing, ROM-only cartridge support, and MBC1 bank switching.
- Implemented the P1 joypad register and basic interrupt-register handling.
- Added a headless CLI for loading a cartridge and executing a selected number of CPU steps.

**What I learned:** The address space is a map of devices, not 64 KiB of interchangeable RAM. Cartridge controllers expose more ROM than can be mapped at once.

### Step 3 — Added timers and interrupt generation

**Goal:** Keep hardware devices in sync with the CPU.

- Implemented divider and timer registers: `DIV`, `TIMA`, `TMA`, and `TAC` (`0xFF04–0xFF07`).
- Added divider-edge-based TIMA increments with selectable clock rates, overflow, delayed reload, and timer interrupt requests.
- Linked peripheral progression to the T-cycles returned from each CPU instruction.
- Added tests for timer register reads/writes, frequencies, overflow, and interaction with CPU HALT and interrupts.

**What I learned:** Many hardware events depend on elapsed CPU cycles. A timer overflow isn't just arithmetic; it interacts with interrupts and the CPU's sleep state.

### Step 4 — Implemented the PPU (graphics)

**Goal:** Render the original Game Boy's 160 × 144 image.

- Added background and window tile decoding, scrolling, DMG palette selection, and sprite rendering.
- Implemented the LCD scanline progression and VBlank/STAT interrupt requests.
- Exposed a framebuffer with four possible pixel shades.
- Added a PPM exporter to inspect rendered output without a window.

**What I learned:** The display is assembled from tile data, sprite attributes, palettes, and scanline timing; it is not a direct bitmap stored in RAM.

### Step 5 — Created the SDL2 frontend

**Goal:** Make the emulator interactive rather than command-line-only.

- Displayed the framebuffer in a scalable SDL2 window using a streaming texture.
- Mapped keyboard input to Game Boy buttons and forwarded button states to the bus.
- Added pause, fast-forward, and close-window handling.
- Added frame pacing based on the Game Boy's roughly 59.73 Hz refresh rate.

**What I learned:** Separating hardware emulation from presentation allows the same core to run with a headless test harness or a graphical frontend.

### Step 6 — Added MBC3 to address Pokémon Red compatibility

**Problem:** When investigating why Pokémon Red could not run, we identified that its common cartridge type, **`0x13` (MBC3 + RAM + battery)**, was rejected by the original cartridge loader.

**Changes:**

- Added support for MBC3 types `0x11`, `0x12`, and `0x13`.
- Implemented switchable ROM banks, external RAM bank selection, and RAM enable/disable.
- Added automatic loading and saving of battery-backed RAM as a `.sav` file beside the ROM.
- Added dedicated MBC3 banking and save-persistence tests.

**Result:** The loader can now accept this class of Pokémon Red cartridge and map its ROM/RAM. **This removed the known mapper error, but did not by itself prove the game boots or plays correctly**; accurate graphics, interrupt, and timing behavior still matter.

### Step 7 — Added the APU and SDL2 audio output

**Goal:** Emulate Game Boy music and sound effects.

- Added two pulse-wave channels, a programmable wave channel, and a noise channel.
- Modelled APU register access in `0xFF10–0xFF3F`, including channel routing and master volume.
- Added length/envelope/sweep updates and a 48 kHz, signed 16-bit stereo output path.
- Queued generated samples for SDL2 playback, with mute support and audio suppression during fast-forward.
- Created automated APU tests.

**What I learned:** An emulator needs to reconcile the hardware's clock with the host computer's sample rate and audio-buffer behavior. Sound correctness depends on timing as well as waveform generation.

## Issues encountered and solutions

| Issue | Cause / investigation | Handling / current outcome |
| --- | --- | --- |
| **Pokémon Red ROM rejected** | Its `0x13` MBC3 cartridge type wasn't supported by the ROM loader. | Extended the cartridge loader with MBC3 ROM/RAM banking and new tests. Acceptance is fixed; full gameplay is not yet verified. |
| **Core test unexpectedly failed near HALT** | A prior test left an interrupt enabled, interfering with an assertion about HALT behavior. | Corrected the test setup to isolate interrupt state; re-ran the suites. |
| **APU sample-count assertion failed** | 4,194,304 CPU cycles per second do not convert to 48,000 audio samples at every arbitrary integer-cycle boundary without fractional rounding. | Updated the test to account for integer sample accumulation/rounding instead of demanding a mathematically impossible exact count at every boundary. |
| **SDL2 frontend couldn't be compiled in the development environment** | SDL2 development headers/libraries weren't installed there. | Preserved the independent core build and unit tests, and documented SDL2 setup for Linux, Windows/MSYS2, and macOS. **The local graphical build still needs validation.** |
| **Graphics aren't cycle-perfect** | The current PPU renders primarily by scanline; exact pixel-fetch timing and bus access contention are complex. | Implemented a usable first-pass renderer with framebuffer tests. Variable mode-3 timing and stricter memory-access rules remain future work. |
| **Timing interactions can differ from hardware** | The timer and PPU advance after complete CPU instructions rather than after individual bus accesses. | Kept cycle accounting explicit and wrote peripheral tests; finer-grained scheduling is planned. |
| **Audio may sound different from real hardware** | The first-pass mixer and frame sequencer simplify analog filtering, DIV coupling, and hardware edge cases. | Added four channels and a stable SDL2 output path, while documenting fidelity limitations. |
| **Save data can be lost on an abrupt exit** | `.sav` writes occur on normal shutdown rather than continuously. | Save on normal exit; exit through Esc/window close and keep backup `.sav` files during development. |

## Screenshots — working ROMs

This section is intentionally prepared for **screenshots of ROMs that have actually been run successfully**. No commercial game's successful execution has been verified here, so the images below are placeholders rather than fabricated screenshots.

Create an `images/` folder and replace these placeholder entries after testing:

| ROM / test | Screenshot | Verification notes |
| --- | --- | --- |
| Pokémon Red | _Add `images/pokemon-red.png`_ | Record whether title screen, gameplay, sound, and save loading work. |
| Tetris or another ROM you have rights to use | _Add `images/tetris.png`_ | Record rendering accuracy and input responsiveness. |
| Homebrew or CPU/PPU test ROM | _Add `images/test-rom.png`_ | Record the test ROM's name and pass/fail output. |

After capturing the images, uncomment and update the following Markdown in this README:

```md
### Pokémon Red

![Pokémon Red running in the emulator]
<img width="659" height="615" alt="image" src="https://github.com/user-attachments/assets/7623455c-ab63-4ff3-9d39-9fb74e4e6e6e" />
<img width="656" height="614" alt="image" src="https://github.com/user-attachments/assets/f9074608-94a6-417e-a56e-5af0110924e2" />
<img width="654" height="609" alt="image" src="https://github.com/user-attachments/assets/d52c5753-b73a-4f1f-b141-e66080de7b06" />


*Tested on: Linux, 8/11/2026.
Title screen: yes.
Gameplay: yes.
Audio: yes.
Saves: yes.*

### Tetris

![Tetris running in the emulator]
<img width="656" height="611" alt="image" src="https://github.com/user-attachments/assets/020aa435-d1b7-4c0a-8d40-ccd60f8690f0" />
<img width="658" height="617" alt="image" src="https://github.com/user-attachments/assets/c7b8645b-f067-4fd3-a9b2-118e6d00f882" />


*Tested on: Linux, 6/11/2026.
Title screen: yes.
Gameplay: yes.

### Dr. Mario
<img width="654" height="614" alt="image" src="https://github.com/user-attachments/assets/06407528-e965-4028-a444-5f4a5167632d" />
<img width="655" height="616" alt="image" src="https://github.com/user-attachments/assets/d12f07c9-fe3c-4e7f-abfd-d652b5885fc0" />

*Tested on: Linux, 6/11/2026.
Title screen: yes.
Gameplay: yes.

```

A useful screenshot should show the actual emulator window, not an unrelated image of a real Game Boy. Capturing the title screen **and** a gameplay frame for a working game provides stronger compatibility evidence than a title screen alone.

## Testing

```bash
make test
```

The latest development run passed six test suites:

1. `test_cpu` — instruction execution and CPU behavior.
2. `test_bus` — memory mapping and CPU/bus integration.
3. `test_timer` — timer frequencies, register behavior, and interrupts.
4. `test_ppu` — framebuffer rendering and display behavior.
5. `test_mbc3` — cartridge banking and save persistence.
6. `test_apu` — audio register and sample-generation behavior.

Tests are valuable **but not equivalent to passing real hardware validation ROMs or demonstrating a working commercial game**. The SDL2 target requires a separate local compilation and live test.

For debugging, use the headless runner to reduce GUI-related variables, inspect registers, and run small or homebrew test ROMs before testing a larger game.

## Known limitations and next steps

- **Improve PPU timing:** variable mode-3 duration, accurate sprite fetch/priorities, VRAM/OAM restrictions, and DMA contention.
- **Improve bus/CPU timing:** account for memory accesses during instructions rather than only at instruction boundaries.
- **Improve APU fidelity:** DIV-driven frame sequencer, better analog/DAC filtering, anti-aliasing, and audio test ROM coverage.
- **Add cartridge compatibility:** RTC-based MBC3 variants, MBC2, MBC5, and additional save/mapper edge cases.
- **Improve serial I/O and remaining I/O behavior** for broader ROM compatibility.
- **Validate end-to-end:** test on the target machine with SDL2, capture real screenshots, and track per-ROM startup/gameplay/audio/save results.

## References

- [Pan Docs — Game Boy technical reference](https://gbdev.io/pandocs/)
- [RGBDS — Game Boy assembly documentation](https://rgbds.gbdev.io/docs/)
- [Blargg's Game Boy test ROMs](https://github.com/retrio/gb-test-roms)
- [SDL2 documentation](https://wiki.libsdl.org/SDL2/FrontPage)

---

**Educational project.** Game Boy, Pokémon, and other game names belong to their respective owners. This repository does not include commercial game ROMs or Nintendo boot ROM files.
