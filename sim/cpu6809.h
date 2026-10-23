/* MC6809 CPU core for sim6809 (no interrupts, no cycle counting) */
#ifndef CPU6809_H
#define CPU6809_H

#include <stdint.h>

/* condition code bits */
enum {
    CC_C = 0x01, CC_V = 0x02, CC_Z = 0x04, CC_N = 0x08,
    CC_I = 0x10, CC_H = 0x20, CC_F = 0x40, CC_E = 0x80
};

/* why the CPU stopped */
enum {
    STOP_NONE = 0,
    STOP_EXIT,      /* program wrote the MMIO exit port */
    STOP_ILLEGAL,   /* illegal opcode or indexed postbyte */
    STOP_TRAP,      /* SWI/SWI2/SWI3/SYNC/CWAI/RTI-with-nothing: unsupported */
};

/* MMIO register offsets from io_base */
enum {
    IO_PUTCH = 0, IO_GETCH = 1, IO_EXIT = 2, IO_DECHI = 4, IO_DECLO = 5,
    IO_SIZE = 16
};

typedef struct Cpu {
    uint8_t a, b, dp, cc;
    uint16_t x, y, u, s, pc;

    uint8_t mem[65536];
    uint16_t io_base;
    uint8_t dec_hi;         /* latched high byte for IO_DECLO */

    int stop;               /* STOP_* */
    int exit_code;
    uint16_t stop_pc;       /* PC of the instruction that stopped */
    const char *stop_msg;
} Cpu;

void cpu_init(Cpu *c, uint16_t io_base, uint8_t fill);
/* execute one instruction; returns c->stop */
int cpu_step(Cpu *c);

uint8_t cpu_read8(Cpu *c, uint16_t addr);
void cpu_write8(Cpu *c, uint16_t addr, uint8_t v);

/* disassemble the instruction at pc into buf; returns its length in bytes.
   Reads memory directly (no MMIO side effects). */
int disasm(const uint8_t *mem, uint16_t pc, char *buf, int buflen);

#endif
