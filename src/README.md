# Game Boy DMG CPU in C

Standalone Sharp SM83 CPU core. The CPU does not own memory: all memory accesses go through callbacks, so you can attach a cartridge/memory bus later.

## Build and test

```sh
make test
```

## How to integrate

```c
#include "cpu.h"
#include <stdint.h>

static uint8_t mem[65536];
static uint8_t bus_read(void *context, uint16_t address) {
    (void)context;
    return mem[address];
}
static void bus_write(void *context, uint16_t address, uint8_t value) {
    (void)context;
    mem[address] = value;
}

int main(void) {
    GB_CPU cpu;
    gb_cpu_init(&cpu, NULL, bus_read, bus_write, true);
    /* ROM-only testing: copy a ROM into mem[0x0000..0x7FFF].
       Real hardware needs cartridge and I/O mapping instead. */
    while (!cpu.invalid_opcode) {
        unsigned tcycles = gb_cpu_step(&cpu);
        if (!tcycles) break;
        /* Later: tick_timer(tcycles), tick_ppu(tcycles), etc. */
        if (cpu.stopped) break;
    }
    return 0;
}
```

## Implemented

- All documented 245 base opcodes and all 256 CB-prefixed opcodes
- 8/16-bit registers, register pairs, stack, ALU and flags
- Conditionals, jumps, calls, returns, reset vectors
- 4 MHz-domain T-cycle counts per opcode and interrupt entry
- Interrupt priority/vectors, EI delay, DI, RETI, HALT/HALT bug
- Illegal opcode detection and initial DMG post-boot register values

## Limitations / next steps

- This is a CPU core, **not** a complete emulator. You still need a cartridge mapper, boot ROM/IO bus, PPU, timer, interrupts sources, joypad and APU.
- STOP wake-up is simplified (wakes on any pending enabled interrupt), and CGB speed switching is not implemented.
- Bus callbacks do not model mid-instruction memory accesses with individual timestamps. For precise PPU/timer synchronization, extend the bus and CPU to tick peripherals at each M-cycle.
- The demo bus treats every address as writable RAM for CPU unit tests, and is not a hardware-accurate MMU.
- ROM compatibility is not guaranteed; validate with Blargg's CPU tests and mooneye-gb's acceptance tests in a complete emulator.

Reference: https://gbdev.io/pandocs/
