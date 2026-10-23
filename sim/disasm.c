/* 6809 disassembler for sim6809 -t traces.  Its instruction lengths must
   agree with cpu6809.c; the len_* sim tests check both against lwasm. */
#include "cpu6809.h"

#include <stdio.h>
#include <string.h>

enum { ILL, INH, IMM8, IMM16, DIR, IDX, EXT, REL8, REL16, PSHS, PSHU, REGS };

static const char *const unary_names[16] = {
    "NEG", NULL, NULL, "COM", "LSR", NULL, "ROR", "ASR",
    "LSL", "ROL", "DEC", NULL, "INC", "TST", "JMP", "CLR",
};

static const char *const branch_names[16] = {
    "BRA", "BRN", "BHI", "BLS", "BHS", "BLO", "BNE", "BEQ",
    "BVC", "BVS", "BPL", "BMI", "BGE", "BLT", "BGT", "BLE",
};

static const char *const alu_a_names[16] = {
    "SUBA", "CMPA", "SBCA", "SUBD", "ANDA", "BITA", "LDA", "STA",
    "EORA", "ADCA", "ORA", "ADDA", "CMPX", "JSR", "LDX", "STX",
};

static const char *const alu_b_names[16] = {
    "SUBB", "CMPB", "SBCB", "ADDD", "ANDB", "BITB", "LDB", "STB",
    "EORB", "ADCB", "ORB", "ADDB", "LDD", "STD", "LDU", "STU",
};

static const char *const reg_names[16] = {
    "D", "X", "Y", "U", "S", "PC", "?", "?",
    "A", "B", "CC", "DP", "?", "?", "?", "?",
};

static const int mode_by_col[4] = { 0, DIR, IDX, EXT };

/* mnemonic and operand mode for an opcode on page 1, 2 or 3 */
static int lookup(int page, int op, char *mn)
{
    const char *s = NULL;
    int mode = ILL, n = op & 0x0f;

    if (page == 2) {
        if (op >= 0x20 && op <= 0x2f) {
            sprintf(mn, "L%s", branch_names[n]);
            return REL16;
        }
        if (op == 0x3f) {
            strcpy(mn, "SWI2");
            return INH;
        }
        if (op >= 0x80) {
            int b = op & 0x40, col = (op >> 4) & 3;
            if (n == 0x3 && !b) s = "CMPD";
            else if (n == 0xc && !b) s = "CMPY";
            else if (n == 0xe) s = b ? "LDS" : "LDY";
            else if (n == 0xf && col != 0) s = b ? "STS" : "STY";
            if (s) {
                strcpy(mn, s);
                return col == 0 ? IMM16 : mode_by_col[col];
            }
        }
        return ILL;
    }
    if (page == 3) {
        if (op == 0x3f) {
            strcpy(mn, "SWI3");
            return INH;
        }
        if (op >= 0x80 && op < 0xc0 && (n == 0x3 || n == 0xc)) {
            int col = (op >> 4) & 3;
            strcpy(mn, n == 0x3 ? "CMPU" : "CMPS");
            return col == 0 ? IMM16 : mode_by_col[col];
        }
        return ILL;
    }

    if (op < 0x10 || (op >= 0x60 && op < 0x80)) {
        if (!unary_names[n])
            return ILL;
        strcpy(mn, unary_names[n]);
        return op < 0x10 ? DIR : op < 0x70 ? IDX : EXT;
    }
    if (op >= 0x40 && op < 0x60) {
        if (!unary_names[n] || n == 0xe)
            return ILL;
        sprintf(mn, "%s%c", unary_names[n], op < 0x50 ? 'A' : 'B');
        return INH;
    }
    if (op >= 0x20 && op < 0x30) {
        strcpy(mn, branch_names[n]);
        return REL8;
    }
    if (op >= 0x80) {
        int b = op & 0x40, col = (op >> 4) & 3;
        int is16 = n == 0x3 || n == 0xc || n == 0xe;
        if (col == 0) {
            if (op == 0x8d) {
                strcpy(mn, "BSR");
                return REL8;
            }
            if (n == 0x7 || n == 0xf || (b && n == 0xd))
                return ILL;
            mode = is16 ? IMM16 : IMM8;
        } else {
            mode = mode_by_col[col];
        }
        strcpy(mn, (b ? alu_b_names : alu_a_names)[n]);
        return mode;
    }

    switch (op) {
    case 0x12: s = "NOP"; mode = INH; break;
    case 0x13: s = "SYNC"; mode = INH; break;
    case 0x16: s = "LBRA"; mode = REL16; break;
    case 0x17: s = "LBSR"; mode = REL16; break;
    case 0x19: s = "DAA"; mode = INH; break;
    case 0x1a: s = "ORCC"; mode = IMM8; break;
    case 0x1c: s = "ANDCC"; mode = IMM8; break;
    case 0x1d: s = "SEX"; mode = INH; break;
    case 0x1e: s = "EXG"; mode = REGS; break;
    case 0x1f: s = "TFR"; mode = REGS; break;
    case 0x30: s = "LEAX"; mode = IDX; break;
    case 0x31: s = "LEAY"; mode = IDX; break;
    case 0x32: s = "LEAS"; mode = IDX; break;
    case 0x33: s = "LEAU"; mode = IDX; break;
    case 0x34: s = "PSHS"; mode = PSHS; break;
    case 0x35: s = "PULS"; mode = PSHS; break;
    case 0x36: s = "PSHU"; mode = PSHU; break;
    case 0x37: s = "PULU"; mode = PSHU; break;
    case 0x39: s = "RTS"; mode = INH; break;
    case 0x3a: s = "ABX"; mode = INH; break;
    case 0x3b: s = "RTI"; mode = INH; break;
    case 0x3c: s = "CWAI"; mode = IMM8; break;
    case 0x3d: s = "MUL"; mode = INH; break;
    case 0x3f: s = "SWI"; mode = INH; break;
    }
    if (s)
        strcpy(mn, s);
    return mode;
}

static uint8_t at(const uint8_t *mem, uint16_t pc, int i)
{
    return mem[(uint16_t)(pc + i)];
}

/* format indexed operand starting at mem[p]; returns bytes used or -1 */
static int fmt_indexed(const uint8_t *mem, uint16_t p, char *out)
{
    static const char rn[4] = { 'X', 'Y', 'U', 'S' };
    uint8_t pb = at(mem, p, 0);
    char r = rn[(pb >> 5) & 3];
    char body[32];
    int len = 1, ind = pb & 0x10;

    if (!(pb & 0x80)) {
        int off = (pb & 0x10) ? (pb & 0x1f) - 32 : (pb & 0x1f);
        sprintf(out, "%d,%c", off, r);
        return 1;
    }
    switch (pb & 0x0f) {
    case 0x0: if (ind) return -1; sprintf(body, ",%c+", r); break;
    case 0x1: sprintf(body, ",%c++", r); break;
    case 0x2: if (ind) return -1; sprintf(body, ",-%c", r); break;
    case 0x3: sprintf(body, ",--%c", r); break;
    case 0x4: sprintf(body, ",%c", r); break;
    case 0x5: sprintf(body, "B,%c", r); break;
    case 0x6: sprintf(body, "A,%c", r); break;
    case 0x8:
        sprintf(body, "%d,%c", (int8_t)at(mem, p, 1), r);
        len = 2;
        break;
    case 0x9:
        sprintf(body, "%d,%c", (int16_t)(at(mem, p, 1) << 8 | at(mem, p, 2)), r);
        len = 3;
        break;
    case 0xb: sprintf(body, "D,%c", r); break;
    case 0xc:
        sprintf(body, "$%04X,PCR", (uint16_t)(p + 2 + (int8_t)at(mem, p, 1)));
        len = 2;
        break;
    case 0xd:
        sprintf(body, "$%04X,PCR",
                (uint16_t)(p + 3 + (at(mem, p, 1) << 8 | at(mem, p, 2))));
        len = 3;
        break;
    case 0xf:
        if (!ind)
            return -1;
        sprintf(body, "$%04X", at(mem, p, 1) << 8 | at(mem, p, 2));
        len = 3;
        break;
    default:
        return -1;
    }
    if (ind)
        sprintf(out, "[%s]", body);
    else
        strcpy(out, body);
    return len;
}

static void fmt_reglist(uint8_t pb, int is_u, char *out)
{
    static const char *const names[8] = { "CC", "A", "B", "DP", "X", "Y", "?", "PC" };
    out[0] = 0;
    for (int i = 0; i < 8; i++) {
        if (!(pb & (1 << i)))
            continue;
        if (out[0])
            strcat(out, ",");
        strcat(out, i == 6 ? (is_u ? "S" : "U") : names[i]);
    }
}

int disasm(const uint8_t *mem, uint16_t pc, char *buf, int buflen)
{
    char mn[8] = "", opnd[40] = "";
    int page = 1, len = 1, op = at(mem, pc, 0), mode;

    if (op == 0x10 || op == 0x11) {
        page = op == 0x10 ? 2 : 3;
        op = at(mem, pc, 1);
        len = 2;
    }
    mode = lookup(page, op, mn);
    uint16_t p = (uint16_t)(pc + len);    /* first operand byte */

    switch (mode) {
    case ILL:
        snprintf(buf, buflen, "???");
        return len;
    case INH:
        break;
    case IMM8:
        sprintf(opnd, "#$%02X", at(mem, p, 0));
        len += 1;
        break;
    case IMM16:
        sprintf(opnd, "#$%04X", at(mem, p, 0) << 8 | at(mem, p, 1));
        len += 2;
        break;
    case DIR:
        sprintf(opnd, "<$%02X", at(mem, p, 0));
        len += 1;
        break;
    case EXT:
        sprintf(opnd, "$%04X", at(mem, p, 0) << 8 | at(mem, p, 1));
        len += 2;
        break;
    case IDX: {
        int k = fmt_indexed(mem, p, opnd);
        if (k < 0) {
            snprintf(buf, buflen, "???");
            return len + 1;
        }
        len += k;
        break;
    }
    case REL8:
        sprintf(opnd, "$%04X", (uint16_t)(p + 1 + (int8_t)at(mem, p, 0)));
        len += 1;
        break;
    case REL16:
        sprintf(opnd, "$%04X", (uint16_t)(p + 2 + (at(mem, p, 0) << 8 | at(mem, p, 1))));
        len += 2;
        break;
    case PSHS:
    case PSHU:
        fmt_reglist(at(mem, p, 0), mode == PSHU, opnd);
        len += 1;
        break;
    case REGS: {
        uint8_t pb = at(mem, p, 0);
        sprintf(opnd, "%s,%s", reg_names[pb >> 4], reg_names[pb & 0xf]);
        len += 1;
        break;
    }
    }
    snprintf(buf, buflen, "%-6s%s", mn, opnd);
    return len;
}
