# plm09: a minimal PL/M to 6809 (lwasm) compiler

This is the design document as built. It started as the approved project
plan; sections now describe what was implemented, and
[Milestones](#milestones) records where the implementation differs from
the plan. The user manuals are `plmc.md` and `sim6809.md` (PDFs alongside).

## Context
`plmc` is a Linux compiler, written in C11, for a "tiny core" subset of PL/M-80. It produces 6809 assembly that lwasm (lwtools 4.24) accepts. Everything machine-specific lives in a hooks file chosen through lwasm's include path. Hooks are provided for the simulator, the Tandy Color Computer under Color BASIC, OS-9/NitrOS-9, and an MC6850 ACIA board (template). With `--os9` the compiler emits position-independent, reentrant OS-9 program modules.

Testing is automated with `sim6809`, a headless 6809 simulator kept in the same repo. The CoCo hooks are also tested automatically on an emulated CoCo 2 under XRoar (the `xroar` suite).

**Status:** milestones M0–M10 are complete. Since then, these have been added:
- the Color Computer hooks, with their two test suites
- the PDF manuals
- OS-9 support (`--os9`, the OS-9 hooks, `sim6809 --os9`, and the `os9` and `nitros9` suites)

Probes of lwasm 4.24 settled several design choices:
- **lwasm defaults to 6309 mode.** Generated files contain `PRAGMA 6809` and every build passes `-9`.
- **`-f srec` carries load addresses and the entry point.** It writes `END label` as the S9 record, and `RMB` emits no bytes. S-records are therefore the format the simulator loads.
- **lwlink can't write srec.** A single lwasm run pulls in the runtime and hooks with `INCLUDE`.
- **Listings need `--list=FILE`.** A separate `-l FILE` fails.
- **`,PCR` forward references** always get a 16-bit offset.

## Repository
The repository is `/home/linville/claude/plm09/`, a git repo with one commit per milestone. All paths below are relative to it.

## Layout
```
Makefile  README.md  CLAUDE.md  .gitignore
src/      main.c util.[ch] lexer.[ch] ast.[ch] parser.[ch] symtab.[ch] sema.[ch] codegen.[ch]
runtime/  plm_crt0.asm  plm_crt0_os9.asm  plm_rt.asm (RT_MUL16, RT_DIV16)
          sim/ coco/ os9/ example/  plm_hooks.asm   (chosen with -I)
sim/      cpu6809.[ch] disasm.c srec.[ch] os9.[ch] main.c
          tests/*.asm + *.out, harness.inc (check macros), gen_len_ops.py (length test)
tests/    run_tests.sh
          lex/*.plm+.tok  parse/*.plm+.ast  err/*.plm+.err
          run/*.plm+.out[+.rc][+.in]
          coco/*.plm+.out[+.in][+.rc][+.screen][+.keys], coco/fakerom.asm
          nitros9/done.asm (completion signal for the nitros9 suite)
          gen/exprgen.c (random expressions + C reference evaluator)
tools/    coco_screen.py (decodes the CoCo text screen from an XRoar snapshot)
docs/     DESIGN.md  plmc.md/.pdf  sim6809.md/.pdf  tex/footnotehyper.sty
```

**Makefile targets**

| Target | Effect |
|---|---|
| `all` | builds `build/plmc` and `build/sim6809` with `-std=c11 -Wall -Wextra -Werror` |
| `test` | runs the suites sim, lex, parse, err, run, coco, xroar, os9, nitros9, fuzz |
| `sim-test`, `lex-test`, `parse-test`, `err-test`, `run-test`, `coco-test`, `xroar-test`, `nitros9-test` | one suite each |
| `os9-test OS9N=25` | the os9 suite, with OS9N fuzz seeds |
| `fuzz-test N=200` | the fuzz suite, with N seeds |
| `run F=x.plm` | compile, assemble and run one program |
| `docs` | build `docs/*.pdf` from the Markdown (pandoc + xelatex) |
| `clean` | remove `build/` |

**Test pipeline** (the `run` suite)
```
plmc [PLMCARGS] t.plm -o t.asm
lwasm -9 -f srec -I runtime -I runtime/sim --list=t.lst -o t.s19 t.asm
sim6809 t.s19 < t.in          # /dev/null if there is no .in file
```
- The runner compares stdout, plus a final `rc=N` line, with `.out` and `.rc` (default 0).
- A first line `/* PLMCARGS: ... */` in the source passes extra compiler options.
- Sim tests accept a first line `; SIMARGS: ...`. Sim tests named `os9_*.asm` are OS-9 modules, assembled with `-f os9` and run with `--os9`.
- Err tests accept a `PLMCARGS` first line too, which is how the option-conflict errors are tested.
- The run suite also checks that compiled code assembles with the `runtime/example` hooks.
- Intermediate files and `.diff` files go to `build/tests/`.

## Language subset (tiny core)
**Source**
- Input is case-insensitive.
- `$` is ignored inside identifiers and numbers.
- Comments are `/* */`.
- Identifiers start with a letter. `_` is not allowed, which keeps generated labels collision-free.
- Numbers take an optional base suffix `H B O Q D`. A hex number must start with a digit. Values must fit in 16 bits.
- Constants of one or two characters (`'A'`, `'AB'`) are numeric values, and `''` is a quote character. Longer strings are rejected.
- All PL/M reserved words are reserved, including ones for features that aren't implemented.

**Program structure**
- A program is `NAME: DO; decls stmts END [NAME];`, optionally followed by `EOF`.
- Declarations: `DECLARE X BYTE`, `DECLARE (A,B) ADDRESS`, `DECLARE A(10) BYTE`, `DECLARE (A,B)(5) ADDRESS`, `DECLARE X LITERALLY 'text'`, or a comma-separated list. They must precede the statements of their block.
- LITERALLY (PL/M-80 manual 6.4) is a parameterless macro whose text, up to 255 characters, is substituted as tokens:
  - Literals are block-scoped, and an inner declaration of any kind hides an outer literal.
  - A name can't be declared twice in one block if either declaration is a literal.
  - A LITERALLY can't be factored or dimensioned.
  - A literal that expands more than 32 levels deep is reported as recursive.
- Arrays:
  - The dimension is a numeric constant, 1–65535, and the array must fit in 64 KB.
  - `A(i)` is an element, usable in expressions and as an assignment target. Subscripts are any expression, zero-extended, and not range-checked.
  - An unsubscripted `A` means `A(0)`.
  - Arrays can't be parameters.
  - `LENGTH(A)`/`LAST(A)` are folded to ADDRESS constants, unless the name is declared by the user.
- Names must be declared before use. Inner declarations hide outer ones.
- Procedures:
  - `P: PROCEDURE (a,b) [BYTE|ADDRESS] [EXTERNAL]; ... END [P];`
  - Parameters must be declared in the procedure body.
  - Procedures may be nested.
  - A typed procedure with no parameters is called by naming it in an expression.
  - The body of an EXTERNAL procedure may only declare its parameters.

**Statements**
- assignment, including multiple targets `A,B(i) = e` (evaluated once)
- `IF/THEN/ELSE`; ELSE binds to the nearest IF
- `[L:] DO; ... END [L];` (may declare variables), and `[L:] DO WHILE e;`, `[L:] DO I = a TO b [BY c];` and `[L:] DO CASE e;`, which may not
- iterative DO follows PL/M-80 manual 5.1.4:
  - the start value is evaluated once
  - the limit is re-evaluated before each pass, and the loop exits when the index is greater than it
  - the step is re-evaluated after each pass, and the loop exits when adding it wraps around
  - all three are converted to the index's type
  - the index must be a scalar variable
- DO CASE: cases count from 0, and null statements are cases. The manual leaves an out-of-range selector undefined; plmc range-checks it and runs no case.
- `CALL`, `RETURN [e]`, `HALT`, and the empty statement

**Operator precedence**, highest first:
1. unary `-` and `+`
2. `* / MOD`
3. `+ -`
4. relationals
5. `NOT`
6. `AND`
7. `OR XOR`

Operators group left to right.

**Semantics** (PL/M-80)
- BYTE is 8-bit unsigned and ADDRESS is 16-bit unsigned. All comparisons are unsigned.
- Constants of 255 or less are BYTE.
- `+ - MOD AND OR XOR`: BYTE op BYTE gives BYTE (wrapping at 8 bits for `+ -`). If either side is ADDRESS, the result is ADDRESS.
- `*` and `/` always give an ADDRESS result. `MOD` follows the `+ -` rule (BYTE MOD BYTE is BYTE). This was verified against the z80pack PL/M-80 language summary; the primary source is Intel's PL/M-80 Programming Manual (9800268B). The plan originally had MOD always giving ADDRESS, and was corrected before M8.
- One-character string constants are BYTE and two-character ones ADDRESS.
- Unary `-` and `NOT` keep their operand's type.
- Relationals give 0FFH or 0.
- IF and WHILE test only bit 0 of the condition.
- AND and OR are evaluated in full, without short-circuiting.
- Assigning ADDRESS to BYTE keeps the low byte. BYTE to ADDRESS zero-extends. Arguments and RETURN values are converted to the declared type.
- Division by zero gives a quotient of 0FFFFH and a remainder equal to the dividend. This is documented as something not to rely on.
- The order in which operands are evaluated is unspecified. The code generator evaluates the right operand first, except when it can be used directly as an instruction operand.

**Procedures** use static storage, as in classic PL/M:
- Parameters and locals get fixed RAM slots, so every access is a plain extended-address access.
- Nested procedures can reach outer variables directly.
- Sema rejects recursion (it finds cycles in the call graph and reports the cycle).
- A typed procedure whose last statement isn't RETURN or HALT gets a warning.
- `Symbol.dim` holds array sizes. `Symbol.storage` is reserved for REENTRANT (frame-relative variables).

**LITERALLY in the parser.** Substitution happens in the parser's token buffer, so sema and codegen never see literals.
- **Lexer:** strings are `T_STRING` tokens. The parser turns one- and two-character strings into constants, and `lex_tokens()` tokenizes a literal's text, located at the place it is used.
- **Scopes:** the parser keeps its own scope stack (`LitScope`) of declared names, pushed for the program, procedures and simple DO blocks.
- **Lazy expansion:** `peek_at(k)` expands positions 0..k, replacing a visible literal name by its tokens, which may expand in turn. Expansion depth is counted per token.
- **Raw names:** names being defined (DECLARE items, procedure and parameter names, labels, the program name, END labels) are read raw, by `next_raw_ident()` / `raw_type()`, and never expanded. This is what lets `DCL TWO BYTE` in an inner block hide an outer literal `TWO`.
- **Mixed lookahead:** `NAME: PROC` works with `PROC LITERALLY 'PROCEDURE'`, because the name and ':' are checked raw and only the third token is expanded.

**Rejected features.** Every unimplemented PL/M feature produces "... is not supported" rather than being misparsed:
- `(*)` dimensions, BASED, STRUCTURE, LABEL declarations
- INITIAL/DATA, `.` (address-of)
- GOTO
- REENTRANT, INTERRUPT, PUBLIC
- PLUS/MINUS, `:=`, ENABLE/DISABLE

The full list of messages is in `plmc.md`.

**Diagnostics** take the form `file:line:col: error: ...`. Compilation stops at the first error. The exit status is 0 on success, 1 on an error, and 2 on bad usage.

## Codegen (src/codegen.c)
**Registers and evaluation**
- A BYTE result goes in B and an ADDRESS result in D. `CLRA` widens B to D, and is emitted only where a 16-bit value is needed (`gen_as`).
- For a binary operator: evaluate the right operand and push it, evaluate the left, then combine with `op ,S+` or `op ,S++`.
- When the right operand is a constant, or a variable of the operation's width, it is used directly as the instruction operand instead of being pushed.
- 16-bit AND/OR/XOR are done as an A part and a B part.
- Each statement is preceded by its source line as a comment.

**Relationals**
- Emit `CMPx`, then use unsigned branches only (BLO/BHS/BHI/BLS/BEQ/BNE).
- To produce a value: `Bcc Lt; CLRB; BRA Le; Lt LDB #$FF`. Don't load B before the branch, because that changes the flags.
- A comparison is 16-bit if either operand is ADDRESS.

**Conditions in IF and WHILE**
- If the condition's root is a relational, emit the compare followed by a long branch on the inverse condition.
- Otherwise use `LSRB` + `LBCC`.
- Statement control flow uses long branches (`LBxx`/`LBRA`).

**Iterative DO** (`gen_iter`):
1. store the start value in the index
2. top: compare the index with the limit and exit when it is greater (`LBHI` when the limit is a simple operand; otherwise the limit is evaluated and compared with `CMPx index` / `LBLO`)
3. the body
4. load the index, add the step (or `#1`), store the result, and `LBCC` back to the top. The carry flag is the wrap test, and ST leaves C alone.

**DO CASE** (`gen_case`):
1. the selector goes into D, then `CMPD #n` / `LBHS end`
2. `LSLB`/`ROLA`, then `LEAX tab,PCR` / `LDD D,X` / `JMP D,X`
3. `tab FDB Lcase-tab ...`: offsets, so the code is position-independent in both modes
4. every case but the last ends with `LBRA end`

**Array elements**
- The parser can't tell `A(i)` from a call, so it builds an `E_CALL`. Sema turns it into `E_INDEX` (with `lhs` = subscript) when `A` is an array.
- Assignment targets are parsed as `E_INDEX` directly.
- **Constant subscript:** `fixed_location()` gives a variable plus a byte offset, which is used as an ordinary operand (`V_A+k` / `V_A+k,U`). This includes the shortcut that uses a simple right operand directly.
- **Computed subscript (`gen_elem_offset`):**
  - the subscript goes into D, doubled with `LSLB`/`ROLA` for ADDRESS arrays
  - the base goes into X (`LDX #V_A`, or `LEAX V_A,U` when position-independent)
  - loads use `LDB D,X` / `LDD D,X`
  - stores push the value, then `LEAX D,X`, pull the value, and `STB`/`STD ,X`

**Multiply and divide**
- BYTE*BYTE uses the `MUL` instruction inline.
- All other cases call `RT_MUL16` or `RT_DIV16` with X = left operand and D = right.
- RT_DIV16 returns the quotient in D and the remainder in X. MOD takes `TFR X,D`.
- Both routines preserve Y and U.

**Calls**
- *Internal procedures:*
  - If no argument contains a call, each argument is evaluated and stored straight into the callee's static parameter slot.
  - Otherwise every argument is evaluated and pushed first, then popped into the slots in reverse order. This makes `F(1,F(2,3))` and `F(F(1,2),F(3,4))` work.
  - Then `JSR`. Results come back in B or D.
- *EXTERNAL procedures (the hook ABI):*
  - Each argument is pushed as a 16-bit word, left to right, and the last argument is also left in D.
  - The caller does `JSR NAME` and then `LEAS 2n,S`.
  - Hooks may change any register except S.
  - An EXTERNAL name may not be a 6809 register name.

**Names in the output**
- global variables `V_X`
- variables and parameters of nested scope n: `Vn_X`
- procedures `P_X` / `Pn_X`
- compiler labels `Lnnn`
- runtime routines and data `RT_*`
- fixed hook entry points `PLM_*`
- externals keep their exact name

These can't clash with user names because PL/M identifiers have no `_`.

**Output file shape**
1. `PRAGMA 6809`
2. `ORG $0100`, or the `--org` value
3. `INCLUDE "plm_crt0.asm"`. `RT_START` does the following:
   - saves the caller's S in `RT_ENTRYS`
   - loads S from `RT_STACKTOP`, unless that is 0, in which case it keeps the caller's stack
   - sets DP=0 (`SETDP 0`)
   - `JSR PLM_MAIN`, then `CLRB`, `JMP PLM_EXIT`
4. `PLM_MAIN` (main program), then each non-EXTERNAL procedure
5. `INCLUDE "plm_rt.asm"` and `INCLUDE "plm_hooks.asm"`. Hooks come after the runtime, so they can call RT_DIV16/RT_MUL16.
6. the `RMB` data section (all variables and parameters)
7. with `--vectors`, `ORG $FFFE` / `FDB RT_START`
8. `END RT_START`

**The hooks contract.** A `plm_hooks.asm` must define:
- `RT_STACKTOP`
- `PLM_EXIT` (exit status in B; must not return)
- `PLM_HALT`
- one label per EXTERNAL procedure the program uses

Other details:
- Mnemonics are always emitted with a leading tab.
- No `@` local labels.
- `HALT` becomes `JMP PLM_HALT`.

**Driver options**
- `-o FILE`
- `--org ADDR` (decimal, `0x`, `$` or trailing `H`)
- `--vectors`
- `--os9`, `--stack N`, `--name NAME` (see [OS-9 modules](#os-9-modules); `--org`/`--vectors` are rejected with `--os9`)
- `--tokens`, `--ast` (debug dumps, used by the golden tests)
- `-h`

## OS-9 modules
`--os9` sets `CodegenOptions.os9`, which turns on position-independent mode (`pic` in `codegen.c`):
- **Variables:** every variable reference goes through `var_ref()`, which appends `,U`.
- **Calls and jumps:** go through `emit_call()`/`emit_jump()`, giving `LBSR`/`LBRA` instead of `JSR`/`JMP`.
- **Other code:** everything else was already relative.

The file becomes an OS-9 module:
1. `MOD RT_EOM,RT_NAME,$11,$81,RT_START,RT_SIZE`
2. the variables as `RMB`. Inside a module lwasm allocates these in the data area; they come first so offsets are known and indexed forms stay short.
3. `RT_NAME FCS /name/` and the edition
4. `INCLUDE "plm_crt0_os9.asm"` (`LBSR PLM_MAIN`, `CLRB`, `LBRA PLM_EXIT`; it keeps OS-9's S, U and DP)
5. the code, the runtime and the hooks (whose `RMB` buffers also go in the data area)
6. `RT_STACK RMB --stack`, `RT_SIZE EQU .`
7. `EMOD`

**Hooks contract in this mode:**
- hooks must preserve U as well as S, and be position-independent
- `RT_STACKTOP` is not used

**Module name.** The name is the program name unless `--name` is given. A name that matches a module already in memory makes OS-9 run that module instead. This was found when the run test `ECHO` ran NitrOS-9's own `echo`; the nitros9 suite therefore names every module after its file, `t<name>`.

**Simulator support.** sim6809 `--os9`:
- loads a module at `--load`, checking the sync bytes, header parity, CRC and type
- sets up registers as `F$Fork` does, with the data area at `--data` and `--param`
- emulates `F$Exit`, `I$Read`, `I$ReadLn`, `I$Write` and `I$WritLn` through `Cpu.swi2_hook`, translating CR to and from newline
- enforces memory protection: writes only to the data area, reads only from the module or the data area, PC only in the module

**What the protection doesn't catch.** With under 256 bytes of data, a variable reference that lost its `,U` would still work, because lwasm assembles it as direct-page addressing and OS-9's DP is the data page. The os9 suite therefore also checks the generated text for `JSR`/`JMP` and for variable operands without `,U`.

## Targets (runtime/*/plm_hooks.asm)
All four provide `PUTCH`, `PUTDEC`, `GETCH` and `EXIT`.

- **sim:**
  - MMIO at $FF00, stack at $FF00
  - GETCH returns 0 at end of input
  - EXIT/HALT become the simulator's exit status
- **coco** (Tandy Color Computer under Color BASIC; uses the standard POLCAT/CHROUT vectors, tested with Disk Extended BASIC 1.1):
  - PUTCH and PUTDEC go through the CHROUT vector ($A002), sending LF as CR, because Color BASIC ignores LF
  - GETCH polls POLCAT ($A000)
  - `CLS` jumps to $A928
  - `RT_STACKTOP = 0`: the program runs on BASIC's stack, and PLM_EXIT/HALT/EXIT return to BASIC with `LDS RT_ENTRYS` / `RTS`
  - Build with `--org 3000H` (below $0E00 is unsafe because $0100–$0111 holds BASIC's interrupt vectors) and `lwasm -f decb`
  - PUTDEC saves Y around ROM calls
- **os9** (OS-9 / NitrOS-9 Level 1 and 2, for `--os9` modules):
  - PUTCH/PUTDEC use `I$Write` on path 1, sending LF as CR; PUTDEC writes its digits and CR in one call
  - GETCH uses `I$Read` on path 0, returning 0 at `E$EOF`
  - EXIT, end of program and HALT use `F$Exit`
  - any other I/O error exits with the error code
  - buffers are `RMB` in the data area
- **example:**
  - MC6850 ACIA template (`ACIA_BASE`, default $A000; `RT_STACKTOP`, default $8000)
  - both can be overridden with lwasm `-D`
  - PUTDEC is built on RT_DIV16
  - exit and HALT spin forever
  - tests only check that it assembles

## Simulator (sim/)
**CPU**
- The full documented MC6809 instruction set and all addressing modes. No interrupts and no cycle accuracy.
- Reset state: all registers 0, including S, and CC=$50.
- Memory starts filled with $A5 (`--zero` for 0), so tests catch use of uninitialized variables.
- Where the datasheet leaves results undefined (H after subtract, V after DAA/SEX, mixed-size TFR/EXG), the choices are documented in `sim6809.md`.
- Undocumented opcodes, reserved indexed postbytes, and SWI/SWI2/SWI3/SYNC/CWAI stop the simulator with a register dump and exit status 125.
- The instruction limit (`-n`, default 100,000,000) stops it with exit status 124.
- Usage and load errors give exit status 2.
- `-t` prints a trace with disassembly to stderr.
- `--io BASE` moves the MMIO block, and `--entry ADDR` overrides the S9 entry address.
- S-records: S1/S9 are loaded, S0/S5 ignored, checksums verified. If several S9 records are present, the last one wins, which lets two files be combined.

**MMIO** (base $FF00, configurable)
| Address | Access | Meaning |
|---|---|---|
| +0 | write | put a character |
| +1 | read | get a character (0 at end of input) |
| +2 | write | exit with that status |
| +4/+5 | write | print the 16-bit value in decimal; `STD $FF04` is enough |

Instruction fetches bypass MMIO; all other accesses go through it.

**Checking the simulator itself**
1. Self-checking assembly tests for flags, branches, indexed modes, PSH/PUL, TFR/EXG, RTI, DAA and MUL. They use `sim/tests/harness.inc`, with expected values worked out by hand from the datasheet at the edge cases.
2. An instruction-length check. `gen_len_ops.py` generates every opcode in every addressing mode as straight-line code (about 11,000 instructions). The traced (address, bytes) pairs must match the lwasm listing (`--list=`).
3. Mutation checks during development. Injected bugs, such as wrong flags or a mis-decoded offset, were confirmed to fail the tests.

## Test suites
| Suite | Contents |
|---|---|
| sim | the checks above, plus the runtime routines (`rt_muldiv`, `rt_hooks`) |
| lex, parse | `--tokens` / `--ast` golden files |
| err | programs that must fail; the `.err` file holds the expected message, with position |
| run | complete programs in sim6809, covering every operator and conversion rule, control flow, input, procedures, HALT, `--org`/`--vectors`, and an assembly check with the example hooks |
| fuzz | `exprgen` seeded random programs against an independent C model of the language rules. They include elements of `BA(4)`/`WA(4)` (constant or `(expr) AND 3` subscripts), assignments to elements, iterative DO loops over random bounds and steps (the model follows the manual's four steps and rejects loops of over 300 passes), and `DO CASE (expr) AND 3`. A failing program is kept as `build/tests/fuzz-fail-SEED.plm` |
| coco | the CoCo hooks in sim6809 with `tests/coco/fakerom.asm`, a stand-in Color BASIC ROM loaded last so its fake EXEC is the entry point. It checks that the program returns with BASIC's S intact, and its CHROUT clobbers Y |
| xroar | the CoCo programs that have a `.screen` file, run on XRoar's `cocous` (CoCo 2, Disk Extended BASIC 1.1); skipped if xroar, `decb` or the ROMs are missing |
| os9 | every run program (except the `--org`/`--vectors` one) and 25 fuzz programs built with `--os9` and run by sim6809 `--os9` at two module/data placements (`$2000`/`$8000` and `$3123`/`$9E00`), with the text check for absolute references, plus `os9 ident` when toolshed is present |
| nitros9 | the same run programs on real NitrOS-9 Level 2, booted from `$NITROS9_DISK` on XRoar's `coco3` with `$COCO3_ROM`; outputs compared, exit statuses not checked; skipped if any prerequisite is missing |

How the xroar suite works:
- The program is written to a disk image with toolshed's `decb`.
- XRoar types `POKE&H2F00,57:LOADM"T":EXEC:EXEC&H2F00`, followed by the `.keys` file.
- It traps at $2F00, which is reached only if the program returned to BASIC, and writes a snapshot.
- `tools/coco_screen.py` decodes the text screen, locating RAM in the snapshot by the program's first bytes.

XRoar details the suite relies on:
- Trap addresses need a `0x` prefix.
- `-load` doesn't place a `.BIN` in RAM, and `-run` bypasses BASIC's EXEC. That is why the suite uses a disk image and `LOADM`.

How the nitros9 suite works:
- Each module and its `.in` file go onto a copy of the boot disk. The copy is first padded to the size its LSN0 header gives (`DD.TOT`). Disk images are often stored truncated, and XRoar then sees fewer cylinders than the file system uses, so files allocated past the end are silently corrupted. This was found when the suite grew to 12 programs.
- The `startup` script starts with `-x`, so that `exit.plm`'s status 42 doesn't abort it. It then runs `t<name> </dd/<name>.in >/dd/<name>.out` for each program, with an empty file as stdin for programs that have no `.in`.
- The script ends with `tdone` (`tests/nitros9/done.asm`), which reads the unused I/O address `$FF6A`. XRoar traps on that read (`-trap read=0xFF6A`) and quits, so no time budget has to be guessed. XRoar's `-timeout` counts emulated seconds and only guards against hangs.
- XRoar writes the disk back, and `os9 copy -l` extracts each output with its CRs turned into newlines.

## Milestones
| # | Milestone | Verification | Status |
|---|---|---|---|
| M0 | Repo, Makefile, stub binaries | `make` builds cleanly under `-Werror` | done |
| M1 | Simulator, srec loader, MMIO, trace | `make sim-test` passes, including the listing-length check | done |
| M2 | Runtime: crt0, sim hooks (PUTCH, PUTDEC, GETCH, EXIT, HALT), MUL16, DIV16 | Assembly harness over edge-case value tables | done |
| M3 | Lexer, `--tokens` | lex goldens and error tests | done |
| M4 | Parser, `--ast` | Goldens for precedence, dangling ELSE, multi-assign, nested procedures | done |
| M5 | Symbol table and sema | Error tests: undeclared, redeclared, argument count, typed/untyped call misuse, recursion | done |
| M6 | Codegen: variables, assignment, `+ - AND OR XOR NOT`, unary minus, EXTERNAL calls | Run tests, including BYTE wrap and truncation | done; the whole code generator landed here, and M7–M9 added its tests |
| M7 | Relationals, IF/ELSE, DO, DO WHILE | Loop tests, the bit-0 rule, comparisons at boundaries | done |
| M8 | `* / MOD` | Run tests and the `exprgen` fuzz test against the C reference | done, with the corrected MOD typing rule |
| M9 | Procedures, parameters and returns | Nested procedures, `F(1,F(2,3))`, a decimal printer and GCD written in PL/M | done |
| M10 | HALT, `--org`, `--vectors`, example hooks, README with the hook ABI, shortcuts | Full suite still passes; one emulator run | done; see the differences below |

**Differences from the plan**
- **Shortcuts:** the constant/variable operand shortcut was done. Shrinking long branches to short ones, and the before/after size measurement, were not.
- **Emulator check:** the single manual XRoar/MAME run was replaced by the automated `xroar` suite.
- **Divide routine:** RT_DIV16 lost a carry branch that analysis showed could never be taken.

**Added after M10**
- the Color Computer hooks
- crt0's `RT_ENTRYS` / `RT_STACKTOP = 0`
- the `coco` and `xroar` suites, and `tools/coco_screen.py`
- the PDF manuals and `make docs`
- OS-9 support: `--os9`/`--stack`/`--name`, `plm_crt0_os9.asm`, `runtime/os9`, sim6809 `--os9` with memory protection, and the `os9` and `nitros9` suites
- arrays, with `LENGTH`/`LAST`; the run tests `arrays` and `bigdata`, and arrays in the fuzz generator
- iterative DO and DO CASE; the run tests `loops` and `case`, and loops and cases in the fuzz generator
- LITERALLY: the lexer's `T_STRING`, parser-level substitution with scopes and raw names, the run test `literally`, a parse golden showing the expansion, and error tests

## Known gaps
- **Language:** BASED, STRUCTURE, INITIAL/DATA (and so `(*)` dimensions), address-of, GOTO, REENTRANT, INTERRUPT, PUBLIC, PLUS/MINUS, `:=`, and built-ins other than LENGTH/LAST (HIGH, LOW, SHL, ...) are not supported. Arrays are one-dimensional and can't be parameters.
- **Code size:** statement control flow always uses long branches.
- **CoCo testing:** the CoCo hooks are tested only on an emulated CoCo 2 with Disk Extended BASIC 1.1, not on a CoCo 3, other ROM versions or real hardware. `CLS` relies on the fixed ROM address $A928.
- **Example hooks:** the ACIA template is only assembled, never run (except once by hand, with its PUTDEC redirected to the simulator).
- **OS-9:**
  - Modules are tested on NitrOS-9 Level 2 only (emulated CoCo 3), not on Level 1 or Microware OS-9.
  - Exit statuses are only checked in the simulator.
  - Command-line parameters (X/D at entry) aren't available to PL/M programs.

## Verification (end-to-end)
- `make && make test` runs every suite. The xroar suite skips itself if XRoar, toolshed or the ROMs are missing.
- `make run F=tests/run/hello.plm` prints through the simulator.
- Every `.asm` the compiler generates must assemble under `lwasm -9` with exit status 0.
- `make docs` rebuilds the manuals.

## Documentation rule
Every changed or added feature updates the matching files in `docs/`, in the same commit:
- `plmc.md` for the compiler, language, hooks or code generation
- `sim6809.md` for the simulator
- this file for architecture, layout, test suites or milestones
- `README.md` when anything users see changes

Then rebuild the PDFs with `make docs`.
