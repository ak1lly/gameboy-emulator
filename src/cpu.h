#ifndef GB_CPU_H
#define GB_CPU_H

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t (*gb_read_fn)(void *context, uint16_t address);
typedef void (*gb_write_fn)(void *context, uint16_t address, uint8_t value);

enum { GB_FLAG_Z = 0x80, GB_FLAG_N = 0x40, GB_FLAG_H = 0x20, GB_FLAG_C = 0x10 };

typedef struct {
    uint8_t a, f, b, c, d, e, h, l;
    uint16_t sp, pc;
    bool ime, halted, stopped, halt_bug, invalid_opcode;
    uint8_t ime_delay;
    uint8_t last_opcode;
    uint64_t cycles;

    void *bus_context;
    gb_read_fn read;
    gb_write_fn write;
} GB_CPU;

/* start_after_boot=true: DMG state with boot ROM skipped (PC=0x0100).
 * false: begin at PC=0 for a separately mapped boot ROM. */
void gb_cpu_init(GB_CPU *cpu, void *bus_context, gb_read_fn read,
                 gb_write_fn write, bool start_after_boot);

/* Runs one instruction or interrupt entry, returns T-cycles (4 per M-cycle).
 * Returns 0 if an illegal opcode is encountered or callbacks are missing.
 * STOP wake-up is simplified: a pending enabled interrupt wakes the CPU. */
unsigned gb_cpu_step(GB_CPU *cpu);

#endif
