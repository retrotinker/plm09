/* MC6809 CPU core for sim6809.
 *
 * Full documented 6809 instruction set and addressing modes, and IRQ, FIRQ
 * and NMI driven through MMIO.  No cycle counting.  SWI/SWI2/SWI3 stop the
 * simulator (STOP_TRAP),
 * except that SWI2 calls swi2_hook when one is set (OS-9 system calls);
 * undocumented opcodes and indexed postbytes stop it (STOP_ILLEGAL).
 */
#include "cpu6809.h"

#include <stdio.h>
#include <string.h>

#define DREG(c) ((uint16_t)((c)->a << 8 | (c)->b))

static void set_d(Cpu *c, uint16_t v)
{
    c->a = (uint8_t)(v >> 8);
    c->b = (uint8_t)v;
}

void cpu_init(Cpu *c, uint16_t io_base, uint8_t fill)
{
    memset(c, 0, sizeof *c);
    memset(c->mem, fill, sizeof c->mem);
    c->io_base = io_base;
    c->cc = CC_I | CC_F;    /* reset state */
    c->timer_lines = INT_IRQ;
}

/* ---------------------------------------------------------------- memory */

static int is_io(Cpu *c, uint16_t addr)
{
    return (uint16_t)(addr - c->io_base) < IO_SIZE;
}

static int in(uint32_t a, uint32_t lo, uint32_t hi)
{
    return a >= lo && a < hi;
}

/* --ft245, --acia: does stdin have a byte?  (-1 at its end) */
static int ft245_ready(void)
{
    int ch = getchar();

    if (ch == EOF)
        return -1;
    ungetc(ch, stdin);
    return 1;
}

uint8_t cpu_read8(Cpu *c, uint16_t addr)
{
    if (c->protect && !in(addr, c->data_lo, c->data_hi) && !in(addr, c->mod_lo, c->mod_hi)) {
        cpu_stop(c, STOP_ILLEGAL, "read outside the module and data area");
        return 0;
    }
    if (c->ft245 && addr == c->ft245_addr) {
        int ch = getchar();
        return ch == EOF ? 0 : (uint8_t)ch;
    }
    if (c->acia && (uint16_t)(addr - c->acia_addr) < 2) {
        if (addr == c->acia_addr) {     /* status: TDRE, and RDRF if waited for */
            if (c->acia_idle < ACIA_IDLE) {
                c->acia_idle++;
                return 0x02;
            }
            if (ft245_ready() < 0 && c->stop_at_end) {
                fflush(stdout);
                c->exit_code = 0;       /* waiting for input that won't come */
                cpu_stop(c, STOP_EXIT, "end of input");
                return 0x02;
            }
            return 0x03;                /* RDRF: data, or the end */
        }
        c->acia_idle = 0;
        int ch = getchar();
        return ch == EOF ? 0 : (uint8_t)ch;
    }
    if (is_io(c, addr)) {
        if (addr - c->io_base == IO_GETCH) {
            int ch = getchar();
            return ch == EOF ? 0 : (uint8_t)ch;
        }
        if (addr - c->io_base == IO_INTSET)
            return c->lines;
        return 0;
    }
    return c->mem[addr];
}

void cpu_write8(Cpu *c, uint16_t addr, uint8_t v)
{
    if (c->protect && !in(addr, c->data_lo, c->data_hi)) {
        cpu_stop(c, STOP_ILLEGAL, "write outside the data area");
        return;
    }
    if (c->ro_hi && in(addr, c->ro_lo, c->ro_hi) && !is_io(c, addr)) {
        if (!c->rom)
            cpu_stop(c, STOP_ILLEGAL, "write to the operating system's memory");
        return;                         /* --rom: ignored, as by real ROM */
    }
    if (c->ft245 && addr == c->ft245_addr) {
        putchar(v);
        return;
    }
    if (c->acia && (uint16_t)(addr - c->acia_addr) < 2) {
        if (addr != c->acia_addr) {     /* control writes are ignored */
            putchar(v);
            c->acia_idle = 0;
        }
        return;
    }
    if (!is_io(c, addr)) {
        c->mem[addr] = v;
        return;
    }
    switch (addr - c->io_base) {
    case IO_PUTCH:
        putchar(v);
        break;
    case IO_EXIT:
        if (!c->stop) {
            c->stop = STOP_EXIT;
            c->exit_code = v;
        }
        break;
    case IO_DECHI:
        c->dec_hi = v;
        break;
    case IO_DECLO:
        printf("%u\n", (unsigned)(c->dec_hi << 8 | v));
        break;
    case IO_INTSET:
        c->lines |= v & (INT_IRQ | INT_FIRQ);
        if (v & INT_NMI)
            c->nmi_pending = 1;
        break;
    case IO_INTCLR:
        c->lines &= (uint8_t)~v;
        break;
    case IO_TIMHI:
        c->tim_hi = v;
        break;
    case IO_TIMLO:
        c->timer_period = c->timer_count = (uint16_t)(c->tim_hi << 8 | v);
        break;
    case IO_TIMLINE:
        c->timer_lines = v & (INT_IRQ | INT_FIRQ | INT_NMI);
        break;
    }
}

static uint16_t read16(Cpu *c, uint16_t addr)
{
    uint8_t hi = cpu_read8(c, addr);
    return (uint16_t)(hi << 8 | cpu_read8(c, (uint16_t)(addr + 1)));
}

static void write16(Cpu *c, uint16_t addr, uint16_t v)
{
    cpu_write8(c, addr, (uint8_t)(v >> 8));
    cpu_write8(c, (uint16_t)(addr + 1), (uint8_t)v);
}

static uint8_t fetch8(Cpu *c)
{
    return c->mem[c->pc++];
}

static uint16_t fetch16(Cpu *c)
{
    uint8_t hi = fetch8(c);
    return (uint16_t)(hi << 8 | fetch8(c));
}

static void push8(Cpu *c, uint16_t *sp, uint8_t v)
{
    --*sp;
    cpu_write8(c, *sp, v);
}

static void push16(Cpu *c, uint16_t *sp, uint16_t v)
{
    push8(c, sp, (uint8_t)v);
    push8(c, sp, (uint8_t)(v >> 8));
}

static uint8_t pull8(Cpu *c, uint16_t *sp)
{
    uint8_t v = cpu_read8(c, *sp);
    ++*sp;
    return v;
}

static uint16_t pull16(Cpu *c, uint16_t *sp)
{
    uint8_t hi = pull8(c, sp);
    return (uint16_t)(hi << 8 | pull8(c, sp));
}

/* ----------------------------------------------------------------- flags */

static void setf(Cpu *c, uint8_t flag, int on)
{
    if (on)
        c->cc |= flag;
    else
        c->cc &= (uint8_t)~flag;
}

static void nz8(Cpu *c, uint8_t r)
{
    setf(c, CC_N, r & 0x80);
    setf(c, CC_Z, r == 0);
}

static void nz16(Cpu *c, uint16_t r)
{
    setf(c, CC_N, r & 0x8000);
    setf(c, CC_Z, r == 0);
}

/* N,Z from result, V cleared: loads, stores, logic ops */
static void logic8(Cpu *c, uint8_t r)
{
    nz8(c, r);
    setf(c, CC_V, 0);
}

static void logic16(Cpu *c, uint16_t r)
{
    nz16(c, r);
    setf(c, CC_V, 0);
}

static uint8_t add8(Cpu *c, uint8_t a, uint8_t b, int carry)
{
    unsigned r = (unsigned)a + b + (carry ? 1 : 0);
    setf(c, CC_H, (a ^ b ^ r) & 0x10);
    setf(c, CC_V, (a ^ r) & (b ^ r) & 0x80);
    setf(c, CC_C, r & 0x100);
    nz8(c, (uint8_t)r);
    return (uint8_t)r;
}

static uint8_t sub8(Cpu *c, uint8_t a, uint8_t b, int borrow)
{
    unsigned r = (unsigned)a - b - (borrow ? 1 : 0);
    setf(c, CC_V, (a ^ b) & (a ^ r) & 0x80);
    setf(c, CC_C, r & 0x100);
    nz8(c, (uint8_t)r);
    return (uint8_t)r;
}

static uint16_t add16(Cpu *c, uint16_t a, uint16_t b)
{
    uint32_t r = (uint32_t)a + b;
    setf(c, CC_V, (a ^ r) & (b ^ r) & 0x8000);
    setf(c, CC_C, r & 0x10000);
    nz16(c, (uint16_t)r);
    return (uint16_t)r;
}

static uint16_t sub16(Cpu *c, uint16_t a, uint16_t b)
{
    uint32_t r = (uint32_t)a - b;
    setf(c, CC_V, (a ^ b) & (a ^ r) & 0x8000);
    setf(c, CC_C, r & 0x10000);
    nz16(c, (uint16_t)r);
    return (uint16_t)r;
}

/* branch condition n = low nibble of the Bcc opcode */
static int cond(Cpu *c, int n)
{
    int C = !!(c->cc & CC_C), V = !!(c->cc & CC_V);
    int Z = !!(c->cc & CC_Z), N = !!(c->cc & CC_N);
    int t;

    switch (n >> 1) {
    case 0: t = 1; break;               /* BRA / BRN */
    case 1: t = !(C | Z); break;        /* BHI / BLS */
    case 2: t = !C; break;              /* BCC / BCS */
    case 3: t = !Z; break;              /* BNE / BEQ */
    case 4: t = !V; break;              /* BVC / BVS */
    case 5: t = !N; break;              /* BPL / BMI */
    case 6: t = N == V; break;          /* BGE / BLT */
    default: t = !Z && N == V; break;   /* BGT / BLE */
    }
    return (n & 1) ? !t : t;
}

/* -------------------------------------------------------------- stopping */

int cpu_stop(Cpu *c, int why, const char *msg)
{
    if (!c->stop) {
        c->stop = why;
        c->stop_msg = msg;
    }
    return c->stop;
}

/* ------------------------------------------------------------ addressing */

static uint16_t *idx_reg(Cpu *c, int r)
{
    switch (r) {
    case 0: return &c->x;
    case 1: return &c->y;
    case 2: return &c->u;
    default: return &c->s;
    }
}

/* decode an indexed postbyte (and its extension bytes) into an EA */
static uint16_t ea_indexed(Cpu *c)
{
    uint8_t pb = fetch8(c);
    uint16_t *r = idx_reg(c, (pb >> 5) & 3);
    uint16_t ea;
    int ind = pb & 0x10;

    if (!(pb & 0x80)) {                 /* 5-bit signed offset */
        int off = (pb & 0x10) ? (pb & 0x1f) - 32 : (pb & 0x1f);
        return (uint16_t)(*r + off);
    }
    switch (pb & 0x0f) {
    case 0x0:
        if (ind)
            return cpu_stop(c, STOP_ILLEGAL, "illegal indexed mode [,R+]"), 0;
        ea = *r;
        *r += 1;
        break;
    case 0x1:
        ea = *r;
        *r += 2;
        break;
    case 0x2:
        if (ind)
            return cpu_stop(c, STOP_ILLEGAL, "illegal indexed mode [,-R]"), 0;
        *r -= 1;
        ea = *r;
        break;
    case 0x3:
        *r -= 2;
        ea = *r;
        break;
    case 0x4: ea = *r; break;
    case 0x5: ea = (uint16_t)(*r + (int8_t)c->b); break;
    case 0x6: ea = (uint16_t)(*r + (int8_t)c->a); break;
    case 0x8: ea = (uint16_t)(*r + (int8_t)fetch8(c)); break;
    case 0x9: ea = (uint16_t)(*r + fetch16(c)); break;
    case 0xb: ea = (uint16_t)(*r + DREG(c)); break;
    case 0xc: {
        int8_t off = (int8_t)fetch8(c);
        ea = (uint16_t)(c->pc + off);
        break;
    }
    case 0xd: {
        uint16_t off = fetch16(c);
        ea = (uint16_t)(c->pc + off);
        break;
    }
    case 0xf:
        if (!ind)
            return cpu_stop(c, STOP_ILLEGAL, "illegal indexed postbyte"), 0;
        ea = fetch16(c);
        break;
    default:
        return cpu_stop(c, STOP_ILLEGAL, "illegal indexed postbyte"), 0;
    }
    return ind ? read16(c, ea) : ea;
}

/* addressing mode from opcode bits 5..4: 1 direct, 2 indexed, 3 extended */
static uint16_t ea_mode(Cpu *c, int mode)
{
    switch (mode) {
    case 1: return (uint16_t)(c->dp << 8 | fetch8(c));
    case 2: return ea_indexed(c);
    default: return fetch16(c);
    }
}

/* ------------------------------------------------------- register moves */

static int reg_is8(int r)
{
    return r >= 8;
}

static uint16_t get_reg(Cpu *c, int r)
{
    switch (r) {
    case 0x0: return DREG(c);
    case 0x1: return c->x;
    case 0x2: return c->y;
    case 0x3: return c->u;
    case 0x4: return c->s;
    case 0x5: return c->pc;
    case 0x8: return c->a;
    case 0x9: return c->b;
    case 0xa: return c->cc;
    case 0xb: return c->dp;
    default: return r >= 8 ? 0xff : 0xffff;
    }
}

static void set_reg(Cpu *c, int r, uint16_t v)
{
    switch (r) {
    case 0x0: set_d(c, v); break;
    case 0x1: c->x = v; break;
    case 0x2: c->y = v; break;
    case 0x3: c->u = v; break;
    case 0x4: c->s = v; break;
    case 0x5: c->pc = v; break;
    case 0x8: c->a = (uint8_t)v; break;
    case 0x9: c->b = (uint8_t)v; break;
    case 0xa: c->cc = (uint8_t)v; break;
    case 0xb: c->dp = (uint8_t)v; break;
    }
}

/* value of register src as seen by a destination register dst.
   Mixed sizes are undefined per Motorola; we mimic the MC6809:
   8->16 gives $FF in the high byte, 16->8 takes the low byte. */
static uint16_t reg_for(Cpu *c, int src, int dst)
{
    uint16_t v = get_reg(c, src);
    if (reg_is8(src) && !reg_is8(dst))
        v |= 0xff00;
    return v;
}

static void op_tfr(Cpu *c, uint8_t pb)
{
    int src = pb >> 4, dst = pb & 0xf;
    set_reg(c, dst, reg_for(c, src, dst));
}

static void op_exg(Cpu *c, uint8_t pb)
{
    int r1 = pb >> 4, r2 = pb & 0xf;
    uint16_t v1 = reg_for(c, r1, r2), v2 = reg_for(c, r2, r1);
    set_reg(c, r2, v1);
    set_reg(c, r1, v2);
}

/* PSHS/PSHU: sp is the stack being pushed, other is the "other" stack reg */
static void op_psh(Cpu *c, uint16_t *sp, uint16_t other, uint8_t pb)
{
    if (pb & 0x80) push16(c, sp, c->pc);
    if (pb & 0x40) push16(c, sp, other);
    if (pb & 0x20) push16(c, sp, c->y);
    if (pb & 0x10) push16(c, sp, c->x);
    if (pb & 0x08) push8(c, sp, c->dp);
    if (pb & 0x04) push8(c, sp, c->b);
    if (pb & 0x02) push8(c, sp, c->a);
    if (pb & 0x01) push8(c, sp, c->cc);
}

static void op_pul(Cpu *c, uint16_t *sp, uint16_t *other, uint8_t pb)
{
    if (pb & 0x01) c->cc = pull8(c, sp);
    if (pb & 0x02) c->a = pull8(c, sp);
    if (pb & 0x04) c->b = pull8(c, sp);
    if (pb & 0x08) c->dp = pull8(c, sp);
    if (pb & 0x10) c->x = pull16(c, sp);
    if (pb & 0x20) c->y = pull16(c, sp);
    if (pb & 0x40) *other = pull16(c, sp);
    if (pb & 0x80) c->pc = pull16(c, sp);
}

/* ----------------------------------------------------- unary (RMW) ops */

/* NEG/COM/LSR/ROR/ASR/ASL/ROL/DEC/INC/TST/CLR selected by the low opcode
   nibble n.  Returns 1 if the result should be stored, 0 for TST,
   -1 if n is not a valid unary op. */
static int unary(Cpu *c, int n, uint8_t m, uint8_t *res)
{
    int cin = !!(c->cc & CC_C);
    uint8_t r;

    switch (n) {
    case 0x0:                           /* NEG */
        r = sub8(c, 0, m, 0);
        break;
    case 0x3:                           /* COM */
        r = (uint8_t)~m;
        logic8(c, r);
        setf(c, CC_C, 1);
        break;
    case 0x4:                           /* LSR */
        setf(c, CC_C, m & 1);
        r = m >> 1;
        nz8(c, r);
        break;
    case 0x6:                           /* ROR */
        setf(c, CC_C, m & 1);
        r = (uint8_t)(m >> 1 | cin << 7);
        nz8(c, r);
        break;
    case 0x7:                           /* ASR */
        setf(c, CC_C, m & 1);
        r = (uint8_t)(m >> 1 | (m & 0x80));
        nz8(c, r);
        break;
    case 0x8:                           /* ASL/LSL */
        setf(c, CC_C, m & 0x80);
        setf(c, CC_V, (m ^ m << 1) & 0x80);
        r = (uint8_t)(m << 1);
        nz8(c, r);
        break;
    case 0x9:                           /* ROL */
        setf(c, CC_C, m & 0x80);
        setf(c, CC_V, (m ^ m << 1) & 0x80);
        r = (uint8_t)(m << 1 | cin);
        nz8(c, r);
        break;
    case 0xa:                           /* DEC */
        r = (uint8_t)(m - 1);
        setf(c, CC_V, m == 0x80);
        nz8(c, r);
        break;
    case 0xc:                           /* INC */
        r = (uint8_t)(m + 1);
        setf(c, CC_V, m == 0x7f);
        nz8(c, r);
        break;
    case 0xd:                           /* TST */
        logic8(c, m);
        return 0;
    case 0xf:                           /* CLR */
        r = 0;
        logic8(c, r);
        setf(c, CC_C, 0);
        break;
    default:
        return -1;
    }
    *res = r;
    return 1;
}

/* opcodes $00-$0F, $60-$6F, $70-$7F: unary op or JMP on memory */
static int mem_unary(Cpu *c, int op)
{
    int n = op & 0x0f;
    int mode = op < 0x10 ? 1 : (op >> 4) - 4;   /* $0x direct, $6x idx, $7x ext */
    uint16_t ea;
    uint8_t r;
    int k;

    if (n == 0x1 || n == 0x2 || n == 0x5 || n == 0xb)
        return cpu_stop(c, STOP_ILLEGAL, "illegal opcode");
    ea = ea_mode(c, mode);
    if (c->stop)
        return c->stop;
    if (n == 0xe) {                     /* JMP */
        c->pc = ea;
        return 0;
    }
    k = unary(c, n, cpu_read8(c, ea), &r);
    if (k > 0)
        cpu_write8(c, ea, r);
    return c->stop;
}

/* --------------------------------------------- accumulator ops $80-$FF */

static int alu(Cpu *c, int op)
{
    int mode = (op >> 4) & 3;           /* 0 imm, 1 dir, 2 idx, 3 ext */
    int bside = op & 0x40;
    int n = op & 0x0f;
    uint8_t *acc = bside ? &c->b : &c->a;
    uint16_t ea = 0;
    uint8_t m8 = 0;
    uint16_t m16 = 0;

    if (!bside && n == 0xd) {           /* BSR / JSR */
        if (mode == 0) {
            int8_t off = (int8_t)fetch8(c);
            push16(c, &c->s, c->pc);
            c->pc = (uint16_t)(c->pc + off);
        } else {
            ea = ea_mode(c, mode);
            if (c->stop)
                return c->stop;
            push16(c, &c->s, c->pc);
            c->pc = ea;
        }
        return c->stop;
    }

    /* stores: STA STB STX STU STD */
    if (n == 0x7 || n == 0xf || (bside && n == 0xd)) {
        if (mode == 0)
            return cpu_stop(c, STOP_ILLEGAL, "illegal opcode (store immediate)");
        ea = ea_mode(c, mode);
        if (c->stop)
            return c->stop;
        if (n == 0x7) {
            cpu_write8(c, ea, *acc);
            logic8(c, *acc);
        } else {
            uint16_t v = n == 0xd ? DREG(c) : bside ? c->u : c->x;
            write16(c, ea, v);
            logic16(c, v);
        }
        return c->stop;
    }

    int is16 = n == 0x3 || n >= 0xc;
    if (mode == 0) {
        if (is16)
            m16 = fetch16(c);
        else
            m8 = fetch8(c);
    } else {
        ea = ea_mode(c, mode);
        if (c->stop)
            return c->stop;
        if (is16)
            m16 = read16(c, ea);
        else
            m8 = cpu_read8(c, ea);
    }

    switch (n) {
    case 0x0: *acc = sub8(c, *acc, m8, 0); break;              /* SUB */
    case 0x1: sub8(c, *acc, m8, 0); break;                     /* CMP */
    case 0x2: *acc = sub8(c, *acc, m8, c->cc & CC_C); break;   /* SBC */
    case 0x3:                                                  /* SUBD/ADDD */
        set_d(c, bside ? add16(c, DREG(c), m16) : sub16(c, DREG(c), m16));
        break;
    case 0x4: *acc &= m8; logic8(c, *acc); break;              /* AND */
    case 0x5: logic8(c, *acc & m8); break;                     /* BIT */
    case 0x6: *acc = m8; logic8(c, m8); break;                 /* LD */
    case 0x8: *acc ^= m8; logic8(c, *acc); break;              /* EOR */
    case 0x9: *acc = add8(c, *acc, m8, c->cc & CC_C); break;   /* ADC */
    case 0xa: *acc |= m8; logic8(c, *acc); break;              /* OR */
    case 0xb: *acc = add8(c, *acc, m8, 0); break;              /* ADD */
    case 0xc:                                                  /* CMPX/LDD */
        if (bside) {
            set_d(c, m16);
            logic16(c, m16);
        } else {
            sub16(c, c->x, m16);
        }
        break;
    case 0xe:                                                  /* LDX/LDU */
        if (bside)
            c->u = m16;
        else
            c->x = m16;
        logic16(c, m16);
        break;
    }
    return c->stop;
}

/* ------------------------------------------------ prefixed pages $10/$11 */

/* SWI, SWI2, SWI3 with --swi: stack everything (E set), mask, vector */
static int software_interrupt(Cpu *c, uint8_t mask, uint16_t vector)
{
    c->cc |= CC_E;
    op_psh(c, &c->s, c->u, 0xff);
    c->cc |= mask;
    c->pc = read16(c, vector);
    return c->stop;
}

static int page2(Cpu *c)
{
    int op = fetch8(c);
    int mode = (op >> 4) & 3;
    uint16_t ea = 0, m16 = 0;

    if (op >= 0x20 && op <= 0x2f) {     /* long conditional branches */
        uint16_t off = fetch16(c);
        if (cond(c, op & 0xf))
            c->pc = (uint16_t)(c->pc + off);
        return 0;
    }
    if (op == 0x3f) {
        if (c->swi2_hook)
            return c->swi2_hook(c);
        if (c->swi)
            return software_interrupt(c, 0, 0xfff4);
        return cpu_stop(c, STOP_TRAP, "SWI2");
    }
    if (op < 0x80)
        return cpu_stop(c, STOP_ILLEGAL, "illegal opcode");

    int n = op & 0x0f, bside = op & 0x40;
    int store = n == 0xf;
    /* valid: CMPD ($x3) and CMPY ($xC) on the A side, LDY/STY on the A side,
       LDS/STS on the B side */
    if (!((n == 0x3 && !bside) || (n == 0xc && !bside) || n == 0xe || n == 0xf))
        return cpu_stop(c, STOP_ILLEGAL, "illegal opcode");
    if (mode == 0 && store)
        return cpu_stop(c, STOP_ILLEGAL, "illegal opcode (store immediate)");

    if (mode == 0) {
        m16 = fetch16(c);
    } else {
        ea = ea_mode(c, mode);
        if (c->stop)
            return c->stop;
        if (!store)
            m16 = read16(c, ea);
    }

    switch (n) {
    case 0x3: sub16(c, DREG(c), m16); break;                   /* CMPD */
    case 0xc: sub16(c, c->y, m16); break;                      /* CMPY */
    case 0xe:                                                  /* LDY/LDS */
        if (bside)
            c->s = m16;
        else
            c->y = m16;
        logic16(c, m16);
        break;
    case 0xf: {                                                /* STY/STS */
        uint16_t v = bside ? c->s : c->y;
        write16(c, ea, v);
        logic16(c, v);
        break;
    }
    }
    return c->stop;
}

static int page3(Cpu *c)
{
    int op = fetch8(c);
    int mode = (op >> 4) & 3;
    uint16_t m16;

    if (op == 0x3f)
        return c->swi ? software_interrupt(c, 0, 0xfff2) : cpu_stop(c, STOP_TRAP, "SWI3");
    if (op < 0x80 || op >= 0xc0 || ((op & 0x0f) != 0x3 && (op & 0x0f) != 0xc))
        return cpu_stop(c, STOP_ILLEGAL, "illegal opcode");
    if (mode == 0) {
        m16 = fetch16(c);
    } else {
        uint16_t ea = ea_mode(c, mode);
        if (c->stop)
            return c->stop;
        m16 = read16(c, ea);
    }
    sub16(c, (op & 0x0f) == 0x3 ? c->u : c->s, m16);           /* CMPU/CMPS */
    return c->stop;
}

/* ------------------------------------------------------------- dispatch */

static void op_daa(Cpu *c)
{
    uint8_t a = c->a, msn = a >> 4, lsn = a & 0xf;
    unsigned cf = 0;

    if ((c->cc & CC_H) || lsn > 9)
        cf |= 0x06;
    if ((c->cc & CC_C) || msn > 9 || (msn > 8 && lsn > 9))
        cf |= 0x60;
    unsigned t = a + cf;
    if (t & 0x100)
        c->cc |= CC_C;
    c->a = (uint8_t)t;
    nz8(c, c->a);
    setf(c, CC_V, 0);
}

/* stack the state and jump through a vector */
static void take(Cpu *c, int entire, uint8_t mask, uint16_t vector)
{
    if (c->waiting != 1) {              /* CWAI stacked it already */
        if (entire)
            c->cc |= CC_E;
        else
            c->cc &= (uint8_t)~CC_E;
        op_psh(c, &c->s, c->u, entire ? 0xff : 0x81);
    }
    c->waiting = 0;
    c->cc |= mask;
    c->pc = read16(c, vector);
}

int cpu_interrupt(Cpu *c)
{
    if (c->stop)
        return TOOK_NONE;
    if (c->ft245) {                     /* RXF#: FIRQ while data is waiting */
        int ready = ft245_ready();
        if (ready > 0)
            c->lines |= INT_FIRQ;
        else
            c->lines &= (uint8_t)~INT_FIRQ;
        if (ready < 0 && c->waiting && !c->timer_period && !c->nmi_pending
            && !(c->lines & INT_IRQ)) {
            fflush(stdout);
            c->exit_code = 0;           /* waiting for input that won't come */
            cpu_stop(c, STOP_EXIT, "end of input");
            return TOOK_NONE;
        }
    }
    if (c->nmi_pending) {
        c->nmi_pending = 0;
        take(c, 1, CC_I | CC_F, 0xfffc);
        return TOOK_NMI;
    }
    if ((c->lines & INT_FIRQ) && !(c->cc & CC_F)) {
        take(c, 0, CC_I | CC_F, 0xfff6);
        return TOOK_FIRQ;
    }
    if ((c->lines & INT_IRQ) && !(c->cc & CC_I)) {
        take(c, 1, CC_I, 0xfff8);
        return TOOK_IRQ;
    }
    if (c->waiting == 2 && (c->lines & (INT_IRQ | INT_FIRQ)))
        c->waiting = 0;                 /* SYNC ends even on a masked line */
    return TOOK_NONE;
}

int cpu_step(Cpu *c)
{
    uint8_t r;
    int op;

    if (c->stop)
        return c->stop;
    if (c->timer_period && --c->timer_count == 0) {
        c->timer_count = c->timer_period;
        c->lines |= c->timer_lines & (INT_IRQ | INT_FIRQ);
        if (c->timer_lines & INT_NMI)
            c->nmi_pending = 1;
    }
    if (c->waiting)
        return 0;
    if (c->pc_hook) {
        c->stop_pc = c->pc;
        int r = c->pc_hook(c);
        if (r >= 0)
            return r;
    }
    c->stop_pc = c->pc;
    if (c->protect && !in(c->pc, c->mod_lo, c->mod_hi))
        return cpu_stop(c, STOP_ILLEGAL, "PC outside the module");
    op = fetch8(c);

    if (op < 0x10 || (op >= 0x60 && op < 0x80))
        return mem_unary(c, op);
    if (op >= 0x80)
        return alu(c, op);
    if (op >= 0x40) {                   /* inherent A / B unary ops */
        uint8_t *acc = op < 0x50 ? &c->a : &c->b;
        if (unary(c, op & 0x0f, *acc, &r) < 0)
            return cpu_stop(c, STOP_ILLEGAL, "illegal opcode");
        if ((op & 0x0f) != 0xd)
            *acc = r;
        return 0;
    }
    if (op >= 0x20 && op < 0x30) {      /* short branches */
        int8_t off = (int8_t)fetch8(c);
        if (cond(c, op & 0xf))
            c->pc = (uint16_t)(c->pc + off);
        return 0;
    }

    switch (op) {
    case 0x10: return page2(c);
    case 0x11: return page3(c);
    case 0x12: return 0;                                        /* NOP */
    case 0x13: c->waiting = 2; return 0;                       /* SYNC */
    case 0x16: {                                                /* LBRA */
        uint16_t off = fetch16(c);
        c->pc = (uint16_t)(c->pc + off);
        return 0;
    }
    case 0x17: {                                                /* LBSR */
        uint16_t off = fetch16(c);
        push16(c, &c->s, c->pc);
        c->pc = (uint16_t)(c->pc + off);
        return c->stop;
    }
    case 0x19: op_daa(c); return 0;
    case 0x1a: c->cc |= fetch8(c); return 0;                    /* ORCC */
    case 0x1c: c->cc &= fetch8(c); return 0;                    /* ANDCC */
    case 0x1d:                                                  /* SEX */
        c->a = (c->b & 0x80) ? 0xff : 0;
        nz16(c, DREG(c));
        return 0;
    case 0x1e: op_exg(c, fetch8(c)); return 0;
    case 0x1f: op_tfr(c, fetch8(c)); return 0;
    case 0x30:                                                  /* LEAX */
    case 0x31: {                                                /* LEAY */
        uint16_t ea = ea_indexed(c);
        if (c->stop)
            return c->stop;
        *(op == 0x30 ? &c->x : &c->y) = ea;
        setf(c, CC_Z, ea == 0);
        return 0;
    }
    case 0x32:                                                  /* LEAS */
    case 0x33: {                                                /* LEAU */
        uint16_t ea = ea_indexed(c);
        if (c->stop)
            return c->stop;
        *(op == 0x32 ? &c->s : &c->u) = ea;
        return 0;
    }
    case 0x34: op_psh(c, &c->s, c->u, fetch8(c)); return c->stop;
    case 0x35: op_pul(c, &c->s, &c->u, fetch8(c)); return c->stop;
    case 0x36: op_psh(c, &c->u, c->s, fetch8(c)); return c->stop;
    case 0x37: op_pul(c, &c->u, &c->s, fetch8(c)); return c->stop;
    case 0x39: c->pc = pull16(c, &c->s); return c->stop;        /* RTS */
    case 0x3a: c->x = (uint16_t)(c->x + c->b); return 0;        /* ABX */
    case 0x3b:                                                  /* RTI */
        c->cc = pull8(c, &c->s);
        if (c->cc & CC_E)
            op_pul(c, &c->s, &c->u, 0x7e);
        c->pc = pull16(c, &c->s);
        return c->stop;
    case 0x3c:                                                  /* CWAI */
        c->cc &= fetch8(c);
        c->cc |= CC_E;
        op_psh(c, &c->s, c->u, 0xff);
        c->waiting = 1;
        return c->stop;
    case 0x3d:                                                  /* MUL */
        set_d(c, (uint16_t)(c->a * c->b));
        setf(c, CC_Z, DREG(c) == 0);
        setf(c, CC_C, c->b & 0x80);
        return 0;
    case 0x3f:                                                  /* SWI */
        return c->swi ? software_interrupt(c, CC_I | CC_F, 0xfffa)
                      : cpu_stop(c, STOP_TRAP, "SWI");
    default:
        return cpu_stop(c, STOP_ILLEGAL, "illegal opcode");
    }
}
