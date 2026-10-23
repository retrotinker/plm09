# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

plm09 is a Linux compiler (`plmc`, written in C11) for a minimal "tiny core" subset of PL/M. It emits 6809 assembly for the lwasm assembler (lwtools 4.24). The target is a bare, generic 6809: all I/O goes through hook routines the user supplies. A headless 6809 simulator (`sim6809`, also C) in this repo runs the generated code in automated tests. XRoar and MAME are installed, but they are for manual spot checks only.

Status: the code is not written yet. The approved design and milestone list (M0–M10) are in `docs/DESIGN.md`. Update this file as each milestone lands.

## Commands (planned Makefile)

- `make` builds `build/plmc` and `build/sim6809` with `-std=c11 -Wall -Wextra -Werror`.
- `make test` runs every suite: sim, lex, parse, err, run, fuzz.
- Single suites: `make sim-test`, `make fuzz-test N=500`, and so on.
- `make run F=tests/run/foo.plm` compiles, assembles and simulates one program.

The pipeline the tests use:
```
build/plmc t.plm -o build/t.asm
lwasm -9 -f srec -I runtime -I runtime/sim -l build/t.lst -o build/t.s19 build/t.asm
build/sim6809 build/t.s19      # stdout and exit code are compared to tests/run/t.out / t.rc
```
Debug flags:
- `plmc --tokens` and `plmc --ast` print lexer and parser output; they are used by the golden tests in `tests/lex` and `tests/parse`.
- `sim6809 -t` prints an instruction trace.
- `sim6809 -n N` sets the instruction limit.

## Architecture

**Compiler pipeline** (`src/`): lexer → recursive-descent parser → AST → symtab/sema → codegen.

What sema does:
- annotates every expression with BYTE or ADDRESS
- checks argument counts and typed/untyped procedure use
- builds a call graph and rejects recursion, because procedures use static storage as in classic PL/M-80

**Semantic rules that are easy to get wrong:**
- All arithmetic and comparisons are unsigned.
- BYTE op BYTE for `+ - AND OR XOR` wraps at 8 bits *before* any widening.
- `* / MOD` always give an ADDRESS result.
- Relationals give 0FFH or 00H.
- IF and WHILE test only bit 0 of the condition.
- AND and OR do not short-circuit.
- Constants of 255 or less are typed BYTE.

**Code generation conventions:**
- BYTE values live in B and ADDRESS values in D.
- A binary operator evaluates its right operand, pushes it, evaluates the left, then combines with `op ,S+` or `op ,S++`.
- Statement control flow uses long branches. Comparisons use unsigned branches only (BLO/BHS/BHI/BLS).
- BYTE*BYTE uses `MUL` inline. Everything else calls `RT_MUL16` or `RT_DIV16` in `runtime/plm_rt.asm`, with X = left and D = right; DIV16 returns the quotient in D and the remainder in X.
- **Internal calls:** push all arguments, pop them into the callee's static parameter slots, `JSR P_name`.
- **EXTERNAL calls (the hook ABI):** each argument is pushed as a 16-bit word, the last one is also left in D, then `JSR NAME` and `LEAS 2n,S`.

**Names in the output.** PL/M identifiers contain no `_`, so these prefixes never collide with user names:

| Name | Meaning |
|---|---|
| `V_X` | global variable |
| `Vn_X` | variable local to procedure scope n |
| `P_X` | procedure |
| `Lnnn` | compiler label |
| `RT_*` | runtime routine |
| `PLM_*` | hook entry point |
| exact name | EXTERNAL procedure |

**Layout of the generated .asm file:**
1. `PRAGMA 6809` and `ORG $0100`
2. `INCLUDE "plm_crt0.asm"`: sets the stack, sets DP=0, `JSR PLM_MAIN`, `JMP PLM_EXIT`
3. the code
4. `INCLUDE` of `plm_rt.asm` and `plm_hooks.asm`
5. the `RMB` data section
6. `END RT_START`

The hooks are selected only through the include path: `-I runtime/sim` for the simulator, `runtime/example/` as the template for real hardware. Compiler output never depends on the target.

**lwasm pitfalls:**
- lwasm defaults to 6309 mode. Always keep `PRAGMA 6809` in the output and pass `-9`.
- Anything in column 1 is a label, so always indent mnemonics with a tab.
- Symbols are case-sensitive.
- Don't use `@` local labels.
- `END label` is required to get the S9 entry record.
- lwlink can't write srec, so everything is assembled in one lwasm run.

**Simulator** (`sim/`):
- Implements the full MC6809 instruction set. No interrupts and no cycle accuracy.
- Loads S-records. Memory is pre-filled with $A5.
- Exit status 125 means an illegal opcode or SWI; 124 means the instruction limit was hit.
- MMIO at $FF00:

  | Address | Access | Meaning |
  |---|---|---|
  | +0 | write | put a character |
  | +1 | read | get a character |
  | +2 | write | exit with that status |
  | +4/+5 | write | print the 16-bit value in decimal; `STD $FF04` is enough |

- The simulator itself is checked by self-checking assembly tests in `sim/tests/` and by comparing trace PCs with the address column of `lwasm -l`.

**Planned extensions** (arrays, BASED, LITERALLY, REENTRANT, iterative DO, DO CASE): `Symbol.dim` and `Symbol.storage` exist from the start for these. Keep them in place, and keep their future words reserved in the lexer.
