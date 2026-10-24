/* sim6809: headless MC6809 simulator for plm09 tests -- driver
 *
 * Exit status: the byte the program wrote to the MMIO exit port,
 * 124 if the instruction limit was reached, 125 on an illegal opcode or
 * unsupported trap, 2 on usage or load errors.
 */
#include "cpu6809.h"
#include "os9.h"
#include "srec.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RC_LIMIT = 124, RC_FAULT = 125, RC_USAGE = 2 };

static void usage(FILE *f)
{
    fprintf(f,
            "usage: sim6809 [options] prog.s19\n"
            "  -t            trace each instruction to stderr\n"
            "  -n N          stop after N instructions (default 100000000)\n"
            "  --io BASE     MMIO base address (default $FF00)\n"
            "  --entry ADDR  start address (default: from S9 record)\n"
            "  --zero        clear memory instead of filling with $A5\n"
            "  --os9         prog is an OS-9 program module (not S-records); emulate\n"
            "                F$Fork and the system calls F$Exit, I$Read, I$ReadLn,\n"
            "                I$Write and I$WritLn\n"
            "  --load ADDR   with --os9: module load address (default $2000)\n"
            "  --data ADDR   with --os9: data area address, page aligned (default $8000)\n"
            "  --param STR   with --os9: parameter string (default empty)\n");
}

/* parse a number: decimal, 0x hex, or $ hex */
static int parse_num(const char *s, unsigned long max, unsigned long *out)
{
    char *end;
    int base = 0;

    if (*s == '$') {
        s++;
        base = 16;
    }
    errno = 0;
    *out = strtoul(s, &end, base);
    return *s && !*end && !errno && *out <= max;
}

static void dump_regs(FILE *f, const Cpu *c)
{
    static const char flags[] = "EFHINZVC";
    char ccs[9];

    for (int i = 0; i < 8; i++)
        ccs[i] = (c->cc & (0x80 >> i)) ? flags[i] : '-';
    ccs[8] = 0;
    fprintf(f, "A=%02X B=%02X X=%04X Y=%04X U=%04X S=%04X DP=%02X CC=%s",
            c->a, c->b, c->x, c->y, c->u, c->s, c->dp, ccs);
}

/* one trace line: address, bytes, disassembly, registers before execution */
static void trace(const Cpu *c)
{
    char text[48], hex[16] = "";
    int len = disasm(c->mem, c->pc, text, sizeof text);

    for (int i = 0; i < len && i < 5; i++)
        sprintf(hex + 2 * i, "%02X", c->mem[(uint16_t)(c->pc + i)]);
    fprintf(stderr, "%04X %-10s %-20s ", c->pc, hex, text);
    dump_regs(stderr, c);
    fputc('\n', stderr);
}

int main(int argc, char **argv)
{
    static Cpu cpu;
    unsigned long limit = 100000000UL, io = 0xff00, entry_opt = 0, v;
    unsigned long load = 0x2000, data = 0x8000;
    int tracing = 0, have_entry_opt = 0, has_entry, os9 = 0;
    const char *params = "";
    uint8_t fill = 0xa5;
    const char *path = NULL;
    uint16_t entry;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(stdout);
            return 0;
        } else if (!strcmp(a, "-t")) {
            tracing = 1;
        } else if (!strcmp(a, "--zero")) {
            fill = 0;
        } else if (!strcmp(a, "--os9")) {
            os9 = 1;
        } else if (i + 1 < argc && !strcmp(a, "--load")) {
            if (!parse_num(argv[++i], 0xffff, &load))
                goto bad;
        } else if (i + 1 < argc && !strcmp(a, "--data")) {
            if (!parse_num(argv[++i], 0xffff, &data))
                goto bad;
        } else if (i + 1 < argc && !strcmp(a, "--param")) {
            params = argv[++i];
        } else if (i + 1 < argc && !strcmp(a, "-n")) {
            if (!parse_num(argv[++i], ~0UL, &limit))
                goto bad;
        } else if (i + 1 < argc && !strcmp(a, "--io")) {
            if (!parse_num(argv[++i], 0xffff - IO_SIZE + 1, &io))
                goto bad;
        } else if (i + 1 < argc && !strcmp(a, "--entry")) {
            if (!parse_num(argv[++i], 0xffff, &entry_opt))
                goto bad;
            have_entry_opt = 1;
        } else if (a[0] != '-' && !path) {
            path = a;
        } else {
            goto bad;
        }
    }
    if (!path)
        goto bad;

    cpu_init(&cpu, (uint16_t)io, fill);
    if (os9) {
        if (os9_load(&cpu, path, (uint16_t)load, (uint16_t)data, params) < 0)
            return RC_USAGE;
        if (have_entry_opt)
            cpu.pc = (uint16_t)entry_opt;
    } else {
        if (srec_load(path, cpu.mem, &entry, &has_entry) < 0)
            return RC_USAGE;
        if (have_entry_opt)
            entry = (uint16_t)entry_opt;
        else if (!has_entry) {
            fprintf(stderr, "sim6809: %s has no S9 entry record; use --entry\n", path);
            return RC_USAGE;
        }
        cpu.pc = entry;
    }

    for (v = 0; v < limit; v++) {
        if (tracing)
            trace(&cpu);
        if (cpu_step(&cpu))
            break;
    }
    fflush(stdout);

    switch (cpu.stop) {
    case STOP_EXIT:
        return cpu.exit_code;
    case STOP_NONE:
        fprintf(stderr, "sim6809: instruction limit (%lu) reached at PC=%04X\n", limit, cpu.pc);
        return RC_LIMIT;
    default:
        fprintf(stderr, "sim6809: %s at PC=%04X\n  ", cpu.stop_msg, cpu.stop_pc);
        dump_regs(stderr, &cpu);
        fputc('\n', stderr);
        return RC_FAULT;
    }

bad:
    usage(stderr);
    return RC_USAGE;
}
