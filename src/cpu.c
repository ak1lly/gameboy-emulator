#include "cpu.h"
#include <string.h>

#define IF_ADDR 0xFF0Fu
#define IE_ADDR 0xFFFFu

static uint8_t rd(GB_CPU *c, uint16_t a) { return c->read(c->bus_context, a); }
static void wr(GB_CPU *c, uint16_t a, uint8_t v) { c->write(c->bus_context, a, v); }
static uint8_t fetch(GB_CPU *c) { return rd(c, c->pc++); }
static uint16_t imm16(GB_CPU *c) {
    uint8_t lo = fetch(c), hi = fetch(c);
    return (uint16_t)((uint16_t)hi << 8 | lo);
}
static uint16_t hl(const GB_CPU *c) { return (uint16_t)((uint16_t)c->h << 8 | c->l); }
static void set_hl(GB_CPU *c, uint16_t v) { c->h = (uint8_t)(v >> 8); c->l = (uint8_t)v; }
static bool flag(const GB_CPU *c, uint8_t mask) { return (c->f & mask) != 0; }
static void flags(GB_CPU *c, bool z, bool n, bool h, bool carry) {
    c->f = (uint8_t)((z ? GB_FLAG_Z : 0) | (n ? GB_FLAG_N : 0) |
                     (h ? GB_FLAG_H : 0) | (carry ? GB_FLAG_C : 0));
}
static uint16_t get_rp(const GB_CPU *c, unsigned p) {
    switch (p) {
    case 0: return (uint16_t)((uint16_t)c->b << 8 | c->c);
    case 1: return (uint16_t)((uint16_t)c->d << 8 | c->e);
    case 2: return hl(c);
    default: return c->sp;
    }
}
static void set_rp(GB_CPU *c, unsigned p, uint16_t v) {
    switch (p) {
    case 0: c->b = (uint8_t)(v >> 8); c->c = (uint8_t)v; break;
    case 1: c->d = (uint8_t)(v >> 8); c->e = (uint8_t)v; break;
    case 2: set_hl(c, v); break;
    default: c->sp = v; break;
    }
}
static uint16_t get_rp2(const GB_CPU *c, unsigned p) {
    return p == 3 ? (uint16_t)((uint16_t)c->a << 8 | c->f) : get_rp(c, p);
}
static void set_rp2(GB_CPU *c, unsigned p, uint16_t v) {
    if (p == 3) { c->a = (uint8_t)(v >> 8); c->f = (uint8_t)v & 0xF0; }
    else set_rp(c, p, v);
}
/* Register encoding B,C,D,E,H,L,(HL),A */
static uint8_t r8(GB_CPU *c, unsigned r) {
    switch (r) {
    case 0: return c->b;
    case 1: return c->c;
    case 2: return c->d;
    case 3: return c->e;
    case 4: return c->h;
    case 5: return c->l;
    case 6: return rd(c, hl(c));
    default: return c->a;
    }
}
static void set_r8(GB_CPU *c, unsigned r, uint8_t v) {
    switch (r) {
    case 0: c->b = v; break;
    case 1: c->c = v; break;
    case 2: c->d = v; break;
    case 3: c->e = v; break;
    case 4: c->h = v; break;
    case 5: c->l = v; break;
    case 6: wr(c, hl(c), v); break;
    default: c->a = v; break;
    }
}
static void push(GB_CPU *c, uint16_t v) {
    wr(c, --c->sp, (uint8_t)(v >> 8));
    wr(c, --c->sp, (uint8_t)v);
}
static uint16_t pop(GB_CPU *c) {
    uint8_t lo = rd(c, c->sp++), hi = rd(c, c->sp++);
    return (uint16_t)((uint16_t)hi << 8 | lo);
}
static bool cond(const GB_CPU *c, unsigned y) {
    switch (y & 3u) {
    case 0: return !flag(c, GB_FLAG_Z);
    case 1: return flag(c, GB_FLAG_Z);
    case 2: return !flag(c, GB_FLAG_C);
    default: return flag(c, GB_FLAG_C);
    }
}
static uint8_t inc8(GB_CPU *c, uint8_t v) {
    uint8_t out = (uint8_t)(v + 1u);
    flags(c, out == 0, false, (v & 0x0F) == 0x0F, flag(c, GB_FLAG_C));
    return out;
}
static uint8_t dec8(GB_CPU *c, uint8_t v) {
    uint8_t out = (uint8_t)(v - 1u);
    flags(c, out == 0, true, (v & 0x0F) == 0, flag(c, GB_FLAG_C));
    return out;
}
static void alu(GB_CPU *c, unsigned op, uint8_t v) {
    unsigned a = c->a, carry = flag(c, GB_FLAG_C) ? 1u : 0u;
    unsigned result = 0;
    switch (op) {
    case 0: /* ADD */
    case 1: /* ADC */
        if (op == 0) carry = 0;
        result = a + v + carry;
        flags(c, (uint8_t)result == 0, false,
              ((a & 15u) + (v & 15u) + carry) > 15u, result > 255u);
        c->a = (uint8_t)result;
        break;
    case 2: /* SUB */
    case 3: /* SBC */
    case 7: /* CP */
        if (op != 3) carry = 0;
        result = a - (unsigned)v - carry;
        flags(c, (uint8_t)result == 0, true,
              (a & 15u) < ((v & 15u) + carry), a < (unsigned)v + carry);
        if (op != 7) c->a = (uint8_t)result;
        break;
    case 4: c->a &= v; flags(c, c->a == 0, false, true, false); break;
    case 5: c->a ^= v; flags(c, c->a == 0, false, false, false); break;
    case 6: c->a |= v; flags(c, c->a == 0, false, false, false); break;
    }
}
static void daa(GB_CPU *c) {
    uint8_t a = c->a;
    bool n = flag(c, GB_FLAG_N), carry = flag(c, GB_FLAG_C);
    unsigned adjust = 0;
    if (!n) {
        if (flag(c, GB_FLAG_H) || (a & 0x0F) > 9) adjust |= 0x06;
        if (carry || a > 0x99) { adjust |= 0x60; carry = true; }
        a = (uint8_t)(a + adjust);
    } else {
        if (flag(c, GB_FLAG_H)) adjust |= 0x06;
        if (carry) adjust |= 0x60;
        a = (uint8_t)(a - adjust);
    }
    c->a = a;
    flags(c, a == 0, n, false, carry);
}
static void add_hl(GB_CPU *c, uint16_t v) {
    uint16_t old = hl(c);
    unsigned result = (unsigned)old + v;
    flags(c, flag(c, GB_FLAG_Z), false,
          ((old & 0x0FFFu) + (v & 0x0FFFu)) > 0x0FFFu, result > 0xFFFFu);
    set_hl(c, (uint16_t)result);
}
static uint16_t add_sp_signed(GB_CPU *c, uint8_t e8) {
    uint16_t sp = c->sp;
    uint16_t result = (uint16_t)(sp + (int8_t)e8);
    flags(c, false, false, ((sp & 0xF) + (e8 & 0xF)) > 0xF,
          ((sp & 0xFF) + e8) > 0xFF);
    return result;
}
static uint8_t rotate_shift(GB_CPU *c, unsigned y, uint8_t v, bool cb) {
    unsigned carry_in = flag(c, GB_FLAG_C) ? 1u : 0u;
    unsigned out_carry = 0;
    uint8_t out = v;
    switch (y) {
    case 0: out_carry = v >> 7; out = (uint8_t)((v << 1) | out_carry); break; /* RLC */
    case 1: out_carry = v & 1u; out = (uint8_t)((v >> 1) | (out_carry << 7)); break; /* RRC */
    case 2: out_carry = v >> 7; out = (uint8_t)((v << 1) | carry_in); break; /* RL */
    case 3: out_carry = v & 1u; out = (uint8_t)((v >> 1) | (carry_in << 7)); break; /* RR */
    case 4: out_carry = v >> 7; out = (uint8_t)(v << 1); break; /* SLA */
    case 5: out_carry = v & 1u; out = (uint8_t)((v >> 1) | (v & 0x80)); break; /* SRA */
    case 6: out = (uint8_t)((v << 4) | (v >> 4)); break; /* SWAP */
    case 7: out_carry = v & 1u; out = v >> 1; break; /* SRL */
    }
    flags(c, cb && out == 0, false, false, out_carry != 0);
    return out;
}
static unsigned exec_cb(GB_CPU *c) {
    uint8_t op = fetch(c);
    unsigned x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    uint8_t v = r8(c, z);
    if (x == 0) set_r8(c, z, rotate_shift(c, y, v, true));
    else if (x == 1) {
        flags(c, (v & (1u << y)) == 0, false, true, flag(c, GB_FLAG_C));
    } else if (x == 2) set_r8(c, z, (uint8_t)(v & ~(1u << y)));
    else set_r8(c, z, (uint8_t)(v | (1u << y)));
    return z == 6 ? (x == 1 ? 12 : 16) : 8;
}

void gb_cpu_init(GB_CPU *c, void *context, gb_read_fn read,
                 gb_write_fn write, bool start_after_boot) {
    memset(c, 0, sizeof(*c));
    c->bus_context = context;
    c->read = read;
    c->write = write;
    if (start_after_boot) {
        c->a = 0x01; c->f = 0xB0;
        c->b = 0x00; c->c = 0x13;
        c->d = 0x00; c->e = 0xD8;
        c->h = 0x01; c->l = 0x4D;
        c->sp = 0xFFFE; c->pc = 0x0100;
    }
}

static unsigned finish(GB_CPU *c, unsigned tcycles, bool executed_opcode) {
    if (executed_opcode && c->ime_delay && --c->ime_delay == 0) c->ime = true;
    c->cycles += tcycles;
    return tcycles;
}

unsigned gb_cpu_step(GB_CPU *c) {
    if (!c || !c->read || !c->write || c->invalid_opcode) return 0;
    uint8_t pending = (uint8_t)(rd(c, IE_ADDR) & rd(c, IF_ADDR) & 0x1F);
    if (c->stopped) {
        if (!pending) return finish(c, 4, false);
        c->stopped = false; /* simplified STOP wake behavior */
    }
    if (c->halted) {
        if (!pending) return finish(c, 4, false);
        c->halted = false;
    }
    if (pending && c->ime) {
        unsigned bit = 0;
        while (!(pending & (1u << bit))) ++bit;
        c->ime = false;
        c->ime_delay = 0;
        wr(c, IF_ADDR, (uint8_t)(rd(c, IF_ADDR) & ~(1u << bit)));
        push(c, c->pc);
        c->pc = (uint16_t)(0x40u + bit * 8u);
        return finish(c, 20, false);
    }

    uint8_t op;
    if (c->halt_bug) { op = rd(c, c->pc); c->halt_bug = false; }
    else op = fetch(c);
    c->last_opcode = op;
    unsigned x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    unsigned p = y >> 1, q = y & 1;
    unsigned cycles = 0;

    if (x == 0) {
        switch (z) {
        case 0:
            if (y == 0) cycles = 4; /* NOP */
            else if (y == 1) { uint16_t a = imm16(c); wr(c, a, (uint8_t)c->sp); wr(c, (uint16_t)(a + 1), (uint8_t)(c->sp >> 8)); cycles = 20; }
            else if (y == 2) { (void)fetch(c); c->stopped = true; cycles = 4; }
            else if (y == 3) { int8_t offset = (int8_t)fetch(c); c->pc = (uint16_t)(c->pc + offset); cycles = 12; }
            else { int8_t offset = (int8_t)fetch(c); bool take = cond(c, y - 4); if (take) c->pc = (uint16_t)(c->pc + offset); cycles = take ? 12 : 8; }
            break;
        case 1:
            if (!q) { set_rp(c, p, imm16(c)); cycles = 12; }
            else { add_hl(c, get_rp(c, p)); cycles = 8; }
            break;
        case 2: {
            uint16_t a = p == 0 ? get_rp(c, 0) : p == 1 ? get_rp(c, 1) : hl(c);
            if (!q) wr(c, a, c->a);
            else c->a = rd(c, a);
            if (p == 2) set_hl(c, (uint16_t)(a + 1));
            if (p == 3) set_hl(c, (uint16_t)(a - 1));
            cycles = 8;
            break;
        }
        case 3: set_rp(c, p, (uint16_t)(get_rp(c, p) + (q ? -1 : 1))); cycles = 8; break;
        case 4: set_r8(c, y, inc8(c, r8(c, y))); cycles = y == 6 ? 12 : 4; break;
        case 5: set_r8(c, y, dec8(c, r8(c, y))); cycles = y == 6 ? 12 : 4; break;
        case 6: set_r8(c, y, fetch(c)); cycles = y == 6 ? 12 : 8; break;
        case 7:
            switch (y) {
            case 0: case 1: case 2: case 3:
                c->a = rotate_shift(c, y, c->a, false); break;
            case 4: daa(c); break;
            case 5: c->a = (uint8_t)~c->a; c->f = (uint8_t)((c->f & (GB_FLAG_Z | GB_FLAG_C)) | GB_FLAG_N | GB_FLAG_H); break;
            case 6: c->f = (uint8_t)((c->f & GB_FLAG_Z) | GB_FLAG_C); break;
            case 7: c->f = (uint8_t)((c->f & GB_FLAG_Z) | (flag(c, GB_FLAG_C) ? 0 : GB_FLAG_C)); break;
            }
            cycles = 4;
            break;
        }
    } else if (x == 1) {
        if (op == 0x76) {
            if (!c->ime && pending) c->halt_bug = true;
            else c->halted = true;
            cycles = 4;
        } else {
            set_r8(c, y, r8(c, z));
            cycles = (y == 6 || z == 6) ? 8 : 4;
        }
    } else if (x == 2) {
        alu(c, y, r8(c, z));
        cycles = z == 6 ? 8 : 4;
    } else {
        switch (z) {
        case 0:
            if (y <= 3) {
                bool take = cond(c, y);
                if (take) c->pc = pop(c);
                cycles = take ? 20 : 8;
            } else if (y == 4) { wr(c, (uint16_t)(0xFF00u + fetch(c)), c->a); cycles = 12; }
            else if (y == 5) { c->sp = add_sp_signed(c, fetch(c)); cycles = 16; }
            else if (y == 6) { c->a = rd(c, (uint16_t)(0xFF00u + fetch(c))); cycles = 12; }
            else { set_hl(c, add_sp_signed(c, fetch(c))); cycles = 12; }
            break;
        case 1:
            if (!q) { set_rp2(c, p, pop(c)); cycles = 12; }
            else if (p == 0) { c->pc = pop(c); cycles = 16; }
            else if (p == 1) { c->pc = pop(c); c->ime = true; c->ime_delay = 0; cycles = 16; }
            else if (p == 2) { c->pc = hl(c); cycles = 4; }
            else { c->sp = hl(c); cycles = 8; }
            break;
        case 2:
            if (y <= 3) { uint16_t addr = imm16(c); bool take = cond(c, y); if (take) c->pc = addr; cycles = take ? 16 : 12; }
            else if (y == 4) { wr(c, (uint16_t)(0xFF00u + c->c), c->a); cycles = 8; }
            else if (y == 5) { wr(c, imm16(c), c->a); cycles = 16; }
            else if (y == 6) { c->a = rd(c, (uint16_t)(0xFF00u + c->c)); cycles = 8; }
            else { c->a = rd(c, imm16(c)); cycles = 16; }
            break;
        case 3:
            if (y == 0) { c->pc = imm16(c); cycles = 16; }
            else if (y == 1) cycles = exec_cb(c);
            else if (y == 6) { c->ime = false; c->ime_delay = 0; cycles = 4; }
            else if (y == 7) { c->ime_delay = 2; cycles = 4; }
            else c->invalid_opcode = true;
            break;
        case 4:
            if (y <= 3) { uint16_t addr = imm16(c); bool take = cond(c, y); if (take) { push(c, c->pc); c->pc = addr; } cycles = take ? 24 : 12; }
            else c->invalid_opcode = true;
            break;
        case 5:
            if (!q) { push(c, get_rp2(c, p)); cycles = 16; }
            else if (p == 0) { uint16_t addr = imm16(c); push(c, c->pc); c->pc = addr; cycles = 24; }
            else c->invalid_opcode = true;
            break;
        case 6: alu(c, y, fetch(c)); cycles = 8; break;
        case 7: push(c, c->pc); c->pc = (uint16_t)(y * 8u); cycles = 16; break;
        }
    }
    if (c->invalid_opcode) return 0;
    return finish(c, cycles, true);
}
