# plm09: a minimal PL/M to 6809 (lwasm) compiler

## Context
This is a new project. `plmc` is a Linux compiler, written in C, for a "tiny core" subset of PL/M. It produces 6809 assembly that lwasm (lwtools 4.24) accepts. The output targets a bare, generic 6809: I/O goes through hook routines the user supplies. Testing is automated with `sim6809`, a headless 6809 simulator kept in the same repo. XRoar and MAME are used only for occasional manual spot checks.

Probes of the installed lwasm 4.24 settled several design choices:
- **lwasm defaults to 6309 mode.** Generated files contain `PRAGMA 6809` and the build passes `-9`.
- **`-f srec` carries load addresses and the entry point.** It writes `END label` as the S9 record, and `RMB` emits no bytes. S-records are therefore the format the simulator loads.
- **lwlink can't write srec.** The first version uses a single lwasm run that pulls in the runtime with `INCLUDE`.

## Step 0: Repo setup
- Create `/home/linville/claude/plm09/` and run `git init` there.
- Add a `.gitignore` for `build/`, `*.o`, `*.s19`, `*.lst` and test outputs.
- All paths below are relative to `plm09/`.

## Layout
```
Makefile  README.md  .gitignore
src/      main.c util.[ch] lexer.[ch] ast.[ch] parser.[ch] symtab.[ch] sema.[ch] codegen.[ch]
runtime/  plm_crt0.asm  plm_rt.asm (RT_MUL16, RT_DIV16)
          sim/plm_hooks.asm   example/plm_hooks.asm   (hooks are chosen with -I)
sim/      cpu6809.[ch] srec.c disasm.c main.c   tests/*.asm + *.out
tests/    run/*.plm+.out[+.rc]  err/*.plm+.err  lex/*.tok  parse/*.ast
          gen/exprgen.c (random expressions + C reference evaluator)  run_tests.sh
```
**Makefile targets**
- `all` builds `build/plmc` and `build/sim6809` with `-std=c11 -Wall -Wextra -Werror`.
- `test` runs sim, lex, parse, err, run and fuzz tests.
- `run F=x.plm`.
- `clean`.

**Test pipeline**
```
plmc t.plm -o t.asm
lwasm -9 -f srec -I runtime -I runtime/sim -o t.s19 t.asm
sim6809 t.s19
```
The test then compares stdout and the exit code against the expected files.

## Language subset (tiny core)
**Source**
- Input is case-insensitive.
- `$` is ignored inside identifiers and numbers.
- Comments are `/* */`.
- Numbers take an optional base suffix `H B O Q D`. A hex number must start with a digit.
- Constants of one or two characters (`'A'`) are numeric values.
- All PL/M reserved words are reserved now, including ones for future features.

**Program structure**
- A program is `NAME: DO; decls stmts END NAME;`.
- Declarations: `DECLARE X BYTE`, `DECLARE (A,B) ADDRESS`, or a comma-separated list.
- Procedures:
  - `P: PROCEDURE (a,b) [BYTE|ADDRESS] [EXTERNAL]; ... END P;`
  - Parameters must be declared in the procedure body.
  - Procedures may be nested.

**Statements**
- assignment, including multiple targets `A,B = e`
- `IF/THEN/ELSE`
- `DO; END` and `DO WHILE`
- `CALL`, `RETURN [e]`, `HALT`, and the empty statement

**Operator precedence**, highest first:
1. unary `-`
2. `* / MOD`
3. `+ -`
4. relationals
5. `NOT`
6. `AND`
7. `OR XOR`

**Semantics** (PL/M-80)
- BYTE is 8-bit unsigned and ADDRESS is 16-bit unsigned. All comparisons are unsigned.
- Constants of 255 or less are BYTE.
- `+ - MOD AND OR XOR`: BYTE op BYTE gives BYTE (wrapping at 8 bits for `+ -`). If either side is ADDRESS, the result is ADDRESS.
- `*` and `/` always give an ADDRESS result. `MOD` follows the `+ -` rule (BYTE MOD BYTE is BYTE). Verified against the z80pack PL/M-80 language summary; the Intel PL/M-80 Programming Manual (9800268B) is the primary source.
- One-character string constants are BYTE and two-character ones ADDRESS.
- Relationals give 0FFH or 0.
- IF and WHILE test only bit 0 of the condition.
- AND and OR are evaluated in full, without short-circuiting.
- Assigning ADDRESS to BYTE keeps the low byte.

**Procedures** use static storage, as in classic PL/M:
- Parameters and locals get fixed RAM slots, so every access is a plain extended-address access.
- Nested procedures can reach outer variables directly.
- Sema rejects recursion (it finds cycles in the call graph).
- `Symbol.storage` and `Symbol.dim` are part of the symbol structure from the start, so REENTRANT, arrays, BASED, LITERALLY, iterative DO and DO CASE can be added later.

## Codegen (src/codegen.c)
**Registers and evaluation**
- A BYTE result goes in B and an ADDRESS result in D. `CLRA` widens B to D.
- For a binary operator: evaluate the right operand and push it, evaluate the left, then combine with `op ,S+` or `op ,S++`.
- When the right operand is a constant or a variable, use it directly instead of pushing it.

**Relationals**
- Emit `CMPx`, then use unsigned branches only (BLO/BHS/BHI/BLS/BEQ/BNE).
- To produce a value: `Bcc Lt; CLRB; BRA Le; Lt LDB #$FF`. Don't load B before the branch, because that changes the flags.

**Conditions in IF and WHILE**
- If the condition's root is a relational, emit the compare followed by a long branch on the inverse condition.
- Otherwise use `LSRB` + `LBCC`.
- Statement control flow uses long branches (`LBxx`/`LBRA`).

**Multiply and divide**
- BYTE*BYTE uses the `MUL` instruction inline.
- All other cases call `RT_MUL16` or `RT_DIV16` with X = left operand and D = right.
- RT_DIV16 returns the quotient in D and the remainder in X.

**Calls**
- *Internal procedures:* evaluate and push all arguments, pop them into the static parameter slots, then `JSR`. Pushing everything first makes `F(1,F(2,3))` work. Results come back in B or D.
- *EXTERNAL procedures (the hook ABI):* each argument is pushed as a 16-bit word and the last argument is also left in D. The caller does `JSR NAME` and then `LEAS 2n,S`.

**Names in the output**
- global variables `V_X`
- procedure-local variables `Vn_X`
- procedures `P_X`
- compiler labels `Lnnn`
- runtime routines `RT_*`
- fixed hook entry points `PLM_*`
- externals keep their exact name

These can't clash with user names because PL/M identifiers have no `_`.

**Output file shape**
1. `PRAGMA 6809`
2. `ORG $0100`
3. `INCLUDE plm_crt0.asm`: sets up the stack, `SETDP 0`, then `JSR PLM_MAIN` and `JMP PLM_EXIT`
4. the code
5. `INCLUDE plm_rt.asm` and `INCLUDE plm_hooks.asm`
6. the `RMB` data section
7. `END RT_START`

Other details:
- Mnemonics are always emitted with a leading tab.
- No `@` local labels.
- `HALT` becomes `JMP PLM_HALT`.
- Options `--org` and `--vectors`.

## Simulator (sim/)
**CPU**
- The full MC6809 instruction set and all addressing modes. No interrupts and no cycle accuracy.
- Illegal opcodes and SWI stop the simulator with a register dump and exit status 125.
- An instruction limit (`-n`) stops it with exit status 124.
- `-t` prints a trace with disassembly.
- Memory starts filled with $A5 so tests catch use of uninitialized variables.

**MMIO** (base $FF00, configurable)
| Address | Access | Meaning |
|---|---|---|
| +0 | write | put a character |
| +1 | read | get a character |
| +2 | write | exit with that status |
| +4/+5 | write | print the 16-bit value in decimal; `STD $FF04` is enough |

**Checking the simulator itself**
1. Self-checking assembly tests for flags, branches, indexed modes, PSH/PUL, TFR/EXG, DAA and MUL, using datasheet values at the edge cases.
2. An instruction-length check: compare the PCs in the trace against the address column of `lwasm -l` for an all-opcodes file.

## Milestones (each one's verification passes before the next starts)
| # | Milestone | Verification |
|---|---|---|
| M0 | Repo, Makefile, stub binaries | `make` builds cleanly under `-Werror` |
| M1 | Simulator, srec loader, MMIO, trace | `make sim-test` passes, including the listing-length check |
| M2 | Runtime: crt0, sim hooks (PUTCH, PUTDEC, EXIT, HALT), MUL16, DIV16 | Assembly harness over edge-case value tables |
| M3 | Lexer, `--tokens` | lex goldens and error tests |
| M4 | Parser, `--ast` | Goldens for precedence, dangling ELSE, multi-assign, nested procedures |
| M5 | Symbol table and sema | Error tests: undeclared, redeclared, argument count, typed/untyped call misuse, recursion |
| M6 | Codegen: variables, assignment, `+ - AND OR XOR NOT`, unary minus, EXTERNAL calls | Run tests, including BYTE wrap and truncation |
| M7 | Relationals, IF/ELSE, DO, DO WHILE | Loop tests, the bit-0 rule, comparisons at boundaries |
| M8 | `* / MOD` | Run tests and the `exprgen` fuzz test against the C reference |
| M9 | Procedures, parameters and returns | Nested procedures, `F(1,F(2,3))`, a decimal printer and GCD written in PL/M |
| M10 | HALT, `--org`, `--vectors`, example hooks, README with the hook ABI, shortcuts | Full suite still passes; one manual XRoar or MAME run |

## Verification (end-to-end)
- `cd plm09 && make && make test` runs every suite.
- `make run F=tests/run/hello.plm` prints through the simulator.
- Every `.asm` the compiler generates must assemble under `lwasm -9` with exit status 0.
- At milestone boundaries, commit to git (only when asked).
