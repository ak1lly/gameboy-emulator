#include "cpu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t mem[65536]; } TestBus;
static uint8_t read8(void *ctx, uint16_t addr) { return ((TestBus *)ctx)->mem[addr]; }
static void write8(void *ctx, uint16_t addr, uint8_t v) { ((TestBus *)ctx)->mem[addr] = v; }
static void start(GB_CPU *cpu, TestBus *bus) {
    memset(bus, 0, sizeof(*bus));
    gb_cpu_init(cpu, bus, read8, write8, true);
}
static void run(GB_CPU *c, int n) { while (n--) assert(gb_cpu_step(c) != 0); }
int main(void) {
    TestBus bus;
    GB_CPU c;
    start(&c, &bus);
    const uint8_t program[] = {0x06,0x05,0x0E,0x03,0x78,0x81,0x76};
    memcpy(bus.mem + 0x100, program, sizeof(program));
    run(&c, 5);
    assert(c.a == 8 && c.b == 5 && c.c == 3 && c.halted);
    assert(c.cycles == 8+8+4+4+4);

    start(&c, &bus);
    const uint8_t arithmetic[] = {0x3e,0x0f,0xc6,0x01,0xce,0xef,0x27,0xfe,0x65,0x76};
    memcpy(bus.mem + 0x100, arithmetic, sizeof(arithmetic));
    run(&c, 3);
    assert(c.a == 0xff && c.f == 0);
    /* DAA after binary arithmetic above is not meaningful BCD; test CP independently */
    start(&c, &bus);
    const uint8_t cp[] = {0x3e,0x10,0xfe,0x20,0x76};
    memcpy(bus.mem + 0x100, cp, sizeof(cp));
    run(&c, 3);
    assert(c.a == 0x10 && c.f == (GB_FLAG_N | GB_FLAG_C));

    start(&c, &bus);
    const uint8_t stack[] = {0x31,0x00,0xd0,0xcd,0x0a,0x01,0x76,0,0,0,0x3e,0x2a,0xc9};
    memcpy(bus.mem + 0x100, stack, sizeof(stack));
    run(&c, 4); /* LD SP, CALL, LD A, RET */
    assert(c.a == 0x2a && c.sp == 0xd000 && c.pc == 0x106);
    run(&c, 1);
    assert(c.halted);

    start(&c, &bus);
    const uint8_t bits[] = {0x06,0x80,0xcb,0x00,0xcb,0x40,0xcb,0xc0,0x76};
    memcpy(bus.mem + 0x100, bits, sizeof(bits));
    run(&c, 2);
    assert(c.b == 1 && c.f == GB_FLAG_C);
    run(&c, 1);
    assert(c.f == (GB_FLAG_H | GB_FLAG_C));

    /* EI takes effect after the following instruction; interrupt entry uses 20 T-cycles. */
    start(&c, &bus);
    bus.mem[0x100] = 0xfb; /* EI */
    bus.mem[0x101] = 0x00; /* NOP */
    bus.mem[0x102] = 0x76; /* HALT */
    bus.mem[0xffff] = 1;  /* IE: VBlank */
    bus.mem[0xff0f] = 1;  /* IF: VBlank */
    assert(gb_cpu_step(&c) == 4 && !c.ime);
    assert(gb_cpu_step(&c) == 4 && c.ime);
    assert(gb_cpu_step(&c) == 20 && c.pc == 0x40 && c.sp == 0xfffc);
    assert(bus.mem[0xfffc] == 0x02 && bus.mem[0xfffd] == 0x01);
    assert((bus.mem[0xff0f] & 1) == 0);

    /* HALT bug: next opcode byte is fetched without incrementing PC. */
    start(&c, &bus);
    bus.mem[0x100] = 0x76; /* HALT while IME=0 and IRQ pending */
    bus.mem[0x101] = 0x3e; /* LD A,d8 */
    bus.mem[0x102] = 0x99;
    bus.mem[0xffff] = 1;
    bus.mem[0xff0f] = 1;
    assert(gb_cpu_step(&c) == 4 && c.halt_bug);
    assert(gb_cpu_step(&c) == 8 && c.a == 0x3e && c.pc == 0x102);

    /* DAA after valid BCD addition. */
    start(&c, &bus);
    const uint8_t bcd[] = {0x3e,0x15,0xc6,0x27,0x27,0x76};
    memcpy(bus.mem + 0x100, bcd, sizeof(bcd));
    run(&c, 3);
    assert(c.a == 0x42 && c.f == 0);

    /* Signed SP arithmetic and half-carry/carry flag behavior. */
    start(&c, &bus);
    const uint8_t sp_prog[] = {0x31,0xff,0x00,0xe8,0x01,0x76};
    memcpy(bus.mem + 0x100, sp_prog, sizeof(sp_prog));
    run(&c, 2);
    assert(c.sp == 0x0100 && c.f == (GB_FLAG_H | GB_FLAG_C));

    /* Smoke-test every opcode, verifying precisely the 11 undefined base opcodes. */
    const uint8_t illegal[] = {0xd3,0xdb,0xdd,0xe3,0xe4,0xeb,0xec,0xed,0xf4,0xfc,0xfd};
    for (unsigned op = 0; op != 256; ++op) {
        start(&c, &bus);
        bus.mem[0x100] = (uint8_t)op;
        bus.mem[0x101] = 0x00;
        bus.mem[0x102] = 0x00;
        unsigned step_cycles = gb_cpu_step(&c);
        bool invalid = false;
        for (unsigned i = 0; i < sizeof(illegal); ++i)
            if (op == illegal[i]) invalid = true;
        assert(c.invalid_opcode == invalid);
        assert((step_cycles == 0) == invalid);
        if (op == 0xcb) {
            for (unsigned ext = 0; ext != 256; ++ext) {
                start(&c, &bus);
                bus.mem[0x100] = 0xcb;
                bus.mem[0x101] = (uint8_t)ext;
                assert(gb_cpu_step(&c) != 0);
                assert(!c.invalid_opcode);
            }
        }
    }

    puts("All CPU tests passed.");
    return 0;
}
