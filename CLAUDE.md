# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

plm09 is a Linux compiler (`plmc`, written in C11) for a minimal "tiny core" subset of PL/M. It emits 6809 assembly for the lwasm assembler (lwtools 4.24). The target is a bare, generic 6809: all I/O goes through hook routines the user supplies. A headless 6809 simulator (`sim6809`, also C) in this repo runs the generated code in automated tests. XRoar and MAME are installed, but they are for manual spot checks only.

**Status:** milestones M0–M10 from `docs/DESIGN.md` are all done: the full tiny-core language compiles and runs. User-facing documentation (language, semantics, hook ABI, options) is in `README.md`; keep it in step with the code. Commit at each milestone.

Requirements: gcc, make, lwasm/lwtools on PATH, and python3 (to generate one sim test and decode XRoar snapshots). The optional xroar suite also uses XRoar with CoCo ROMs in `~/.xroar/roms` and toolshed's `decb`.

## Commands

- `make` builds `build/plmc` and `build/sim6809` with `-std=c11 -Wall -Wextra -Werror`. Warnings are errors.
- `make test` runs every suite in this order: sim, lex, parse, err, run, coco, xroar, fuzz.
- A single suite: `make sim-test`, `make lex-test`, `make parse-test`, `make err-test`, `make run-test`, `make coco-test`, `make xroar-test`, or `make fuzz-test N=500`.
- `make run F=prog.plm` compiles, assembles and simulates one program, then prints its output.
- `make clean` removes `build/`.
- `make docs` rebuilds `docs/plmc.pdf` and `docs/sim6809.pdf` from their Markdown sources with pandoc + xelatex. `docs/tex/footnotehyper.sty` is a stand-in for a package this TeX installation lacks. The PDFs are committed, so when compiler or simulator behavior, options or messages change, update the `.md` manual as well as README.md, and rebuild.

The suites are run by `tests/run_tests.sh <suite>`. On a mismatch it prints a diff and leaves `*.diff` files and every intermediate (`.asm`, `.lst`, `.s19`, stdout, stderr) in `build/tests/`. There is no single-test switch. To repeat one test by hand, use the same commands the runner uses:
```
build/plmc t.plm -o build/t.asm
lwasm -9 -f srec -I runtime -I runtime/sim --list=build/t.lst -o build/t.s19 build/t.asm
build/sim6809 build/t.s19 </dev/null; echo rc=$?
```

**Test file conventions:**
- **sim:** `sim/tests/X.asm` + `X.out`. The expected output is stdout followed by a last line `rc=N`. An optional first line `; SIMARGS: -n 1000` passes extra options to the simulator.
- **lex / parse:** `tests/lex/X.plm` + `.tok` and `tests/parse/X.plm` + `.ast`. These are golden files compared with `plmc --tokens` and `plmc --ast` output.
- **err:** `tests/err/X.plm` + `.err`. Compilation must fail, and its stderr must contain the first line of `.err`.
- **run:** `tests/run/X.plm` + `.out`, plus optional `.rc` (expected exit status, default 0) and `.in` (fed to stdin). An optional first line `/* PLMCARGS: ... */` passes extra compiler options. The suite also checks that compiled code assembles with `runtime/example/` hooks.
- **coco:** `tests/coco/X.plm` + `.out` (+ `.in`, `.rc`), compiled with `--org 3000H` against `runtime/coco` hooks and run in sim6809 together with `tests/coco/fakerom.asm`. The fake ROM is loaded last so its S9 entry (a fake `EXEC`) wins. Output keeps the CoCo's CR newlines, so write `.out` files with `printf '...\r'`.
- **xroar:** the `tests/coco` programs that have a `.screen` file (and optional `.keys`, typed after `EXEC`), run on an emulated CoCo 2 (machine `cocous`, Disk Extended BASIC 1.1). Start each test program with `CALL CLS` so the screen doesn't depend on the ROM's banner. XRoar facts this suite relies on:
  - Trap addresses need a `0x` prefix.
  - `-load` doesn't put a `.BIN` in RAM, and `-run` starts it without going through BASIC's `EXEC`. That's why the suite builds a disk image with `decb` and types `LOADM`.
  - `tools/coco_screen.py` finds RAM in the snapshot by the program's first bytes (last occurrence, because Disk BASIC's buffer holds a copy) instead of parsing the format.
- **fuzz:** `tests/gen/exprgen.c SEED out.plm out.expected` writes a random expression program and computes its expected output with its own C model of the typing rules. A failing program is kept as `build/tests/fuzz-fail-SEED.plm`. If you change a typing rule, change it in both `src/sema.c` and `exprgen.c`.

**sim6809 options:**

| Option | Effect |
|---|---|
| `-t` | trace each instruction to stderr (address, bytes, disassembly, registers before execution) |
| `-n N` | instruction limit (default 100M) |
| `--io BASE` | move the MMIO base |
| `--entry ADDR` | start address when there is no S9 record |
| `--zero` | clear memory instead of filling it with $A5 |

**sim6809 exit status:**

| Status | Meaning |
|---|---|
| byte written to the exit port | normal exit |
| 124 | instruction limit reached |
| 125 | illegal opcode, or SWI/SWI2/SWI3/SYNC/CWAI (registers are dumped to stderr) |
| 2 | usage or load error |

## Architecture

### Simulator (`sim/`, implemented)

| File | Contents |
|---|---|
| `cpu6809.c` | the CPU core: full MC6809 instruction set, no interrupts or cycle counts |
| `disasm.c` | table-driven disassembler, used only by `-t` |
| `srec.c` | S1/S9 loader |
| `main.c` | the driver |

Instruction lengths are decoded separately in `cpu6809.c` and `disasm.c`. The `len_ops` test keeps both in agreement with lwasm, so any change to decoding must keep `make sim-test` passing.

MMIO at $FF00 (`IO_*` in `cpu6809.h`):

| Address | Access | Meaning |
|---|---|---|
| +0 | write | put a character |
| +1 | read | get a character (0 at EOF) |
| +2 | write | exit with that status |
| +4/+5 | write | print the 16-bit value in decimal and a newline; the write to +5 triggers it, so `STD $FF04` is enough |

How the simulator itself is tested:
- **Hand-checked tests:** most `sim/tests/*.asm` files `INCLUDE harness.inc`. It provides:
  - check groups: `TBEGIN`/`TEND` print `OK name`
  - inline checks: `JSR CHKA` + `FCB expected`, and `JSR CHKD` + `FDB expected`
  - table-driven `RUN8`/`RUN16` for ALU test vectors

  The expected values are worked out by hand from the datasheet, so when a test fails, check the expected value as well as the simulator. Watch for flag clobbering inside tests: LD/ST set N, Z and V, while TFR, LEAS/LEAU and PSH/PUL (except PULS CC) leave CC alone.
- **Length check:** `gen_len_ops.py` generates every opcode × addressing mode as straight-line code into `build/tests/`. The runner checks that each traced (address, bytes) pair between `LEN_BEGIN` and `LEN_END` matches the lwasm listing.

### Compiler (`src/`)

The pipeline is `lexer.c` → `parser.c` (recursive descent, builds `ast.h` nodes) → `sema.c` (with `symtab.c`) → `codegen.c`, and `main.c` is the driver. Each stage reports the first error through `error_at()` in `util.c` and exits. Error tests match these messages, so a reworded message means updating `tests/err/*.err`.

- **Lexer and parser:** the parser pulls tokens from the lexer through a 3-token lookahead buffer.
- **Unsupported features** (arrays, LITERALLY, iterative DO, and so on) are rejected in the parser with "X is not supported".
- **Declaration order:** sema processes declarations in source order (declare before use), which is also how recursion stays detectable. A procedure's name is visible inside its own body.
- **Output of sema:** it gives every `Expr` a `type`, resolves `sym`, sets `Stmt.proc` for RETURN, and fills `sema_vars` (the data section) and `sema_procs` (the order procedures are emitted in).

What sema does:
- annotates every expression with BYTE or ADDRESS
- checks argument counts and typed/untyped procedure use
- builds a call graph and rejects recursion, because procedures use static storage as in classic PL/M-80

Debug output: `plmc --tokens` and `plmc --ast`.

**Semantic rules that are easy to get wrong:**
- All arithmetic and comparisons are unsigned.
- BYTE op BYTE for `+ - MOD AND OR XOR` gives BYTE; `+ -` wrap at 8 bits *before* any widening.
- `*` and `/` always give an ADDRESS result. `MOD` follows the `+ -` rule: BYTE MOD BYTE is BYTE. (Checked against the z80pack PL/M-80 summary; Intel manual 9800268B is the primary source.)
- One-character string constants are BYTE, two-character ones ADDRESS.
- Relationals give 0FFH or 00H.
- IF and WHILE test only bit 0 of the condition.
- AND and OR do not short-circuit.
- Constants of 255 or less are typed BYTE.

**Code generation conventions:**
- BYTE values live in B and ADDRESS values in D.
- A binary operator evaluates its right operand, pushes it, evaluates the left, then combines with `op ,S+` or `op ,S++`.
- Statement control flow uses long branches. Comparisons use unsigned branches only (BLO/BHS/BHI/BLS).
- A BYTE result is widened with `CLRA` only where a 16-bit value is needed (`gen_as`).
- Relational results are produced as values with a short branch. IF and WHILE conditions skip that and branch straight on the compare (`gen_cond_false`).
- BYTE*BYTE uses `MUL` inline. Everything else calls `RT_MUL16` or `RT_DIV16` in `runtime/plm_rt.asm`, with X = left and D = right; DIV16 returns the quotient in D and the remainder in X.
- **Internal calls:** if no argument contains a call, each argument is stored straight into the callee's static parameter slot. Otherwise all arguments are pushed first and then popped into the slots, so that `F(1,F(2,3))` works. Then `JSR P_name`.
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

The hooks are selected only through the include path: `-I runtime/sim` for the simulator, `runtime/coco/` for a Color Computer under Color BASIC, and `runtime/example/` (MC6850 ACIA) as the template for other hardware. `RT_STACKTOP` = 0 makes `crt0` keep the caller's stack, which the CoCo hooks use to return to BASIC through `RT_ENTRYS`. Compiler output never depends on the target. The runtime and hooks are included *after* the code, so hooks can call `RT_DIV16`/`RT_MUL16`.

**Planned extensions** (arrays, BASED, LITERALLY, REENTRANT, iterative DO, DO CASE): `Symbol.dim` and `Symbol.storage` exist from the start for these. Keep them in place, and keep their future words reserved in the lexer.

## lwasm pitfalls

- lwasm defaults to 6309 mode. Always keep `PRAGMA 6809` in the output and pass `-9`.
- The listing needs `--list=FILE`; a separate `-l FILE` fails.
- Anything in column 1 is a label, so always indent mnemonics with a tab.
- Symbols are case-sensitive.
- Don't use `@` local labels.
- `END label` is required to get the S9 entry record.
- A forward reference with `,PCR` always gets a 16-bit offset.
- lwlink can't write srec, so everything is assembled in one lwasm run.
