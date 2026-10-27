# plm09: a minimal PL/M to 6809 (lwasm) compiler

This is the design document as built. It started as the approved project
plan; sections now describe what was implemented, and
[Milestones](#milestones) records where the implementation differs from
the plan. The user manuals are `plmc.md` and `sim6809.md` (PDFs alongside).

## Context
`plmc` is a Linux compiler, written in C11, for a "tiny core" subset of PL/M-80. It produces 6809 assembly that lwasm (lwtools 4.24) accepts. Everything machine-specific lives in a hooks file chosen through lwasm's include path. Hooks are provided for the simulator, the Tandy Color Computer under Color BASIC, OS-9/NitrOS-9, FLEX 9, and an MC6850 ACIA board (template). With `--os9` the compiler emits position-independent, reentrant OS-9 program modules.

Testing is automated with `sim6809`, a headless 6809 simulator kept in the same repo. The CoCo hooks are also tested automatically on an emulated CoCo 2 under XRoar (the `xroar` suite).

**Status:** milestones M0–M10 are complete. Since then, these have been added:
- the Color Computer hooks, with their two test suites
- the PDF manuals
- OS-9 support (`--os9`, the OS-9 hooks, `sim6809 --os9`, and the `os9` and `nitros9` suites)
- FLEX support (the FLEX hooks, `srec2flex`, `sim6809 --flex`, and the `flex` suite)

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
Makefile  README.md  CLAUDE.md  LICENSE (MIT)  plm09.spec (Fedora RPM)  .gitignore
src/      main.c util.[ch] lexer.[ch] ast.[ch] parser.[ch] symtab.[ch] sema.[ch] codegen.[ch]
runtime/  plm_crt0.asm  plm_crt0_os9.asm  plm_rt.asm (RT_MUL16, RT_DIV16)
          sim/ coco/ os9/ flex/ example/  plm_hooks.asm   (chosen with -I)
sim/      cpu6809.[ch] (also the --acia, --ft245 and --swi devices) disasm.c srec.[ch] os9.[ch] flex.[ch] main.c
          tests/*.asm + *.out, harness.inc (check macros), gen_len_ops.py (length test)
tests/    run_tests.sh
          lex/*.plm+.tok  parse/*.plm+.ast  err/*.plm+.err
          run/*.plm+.out[+.rc][+.in]
          coco/*.plm+.out[+.in][+.rc][+.screen][+.keys], coco/fakerom.asm
          nitros9/done.asm (completion signal for the nitros9 suite)
          gen/exprgen.c (random expressions + C reference evaluator)
tools/    coco_screen.py (decodes the CoCo text screen from an XRoar snapshot)
          srec2flex.c (S-records to a FLEX .CMD file; built as build/srec2flex)
          rpmdocs.py (the RPM's documentation patch: runtime paths to the installed ones)
docs/     DESIGN.md  plmc.md/.pdf  sim6809.md/.pdf  tex/footnotehyper.sty
          man/plmc.1  man/sim6809.1  man/srec2flex.1 (man pages)
```

**Makefile targets**

| Target | Effect |
|---|---|
| `all` | builds `build/plmc`, `build/sim6809` and `build/srec2flex` with `-std=c11 -Wall -Wextra $(WERROR) $(CPPFLAGS) $(CFLAGS)` and `$(LDFLAGS)`; `CFLAGS` (default `-O2 -g`) and `LDFLAGS` can be replaced, as rpmbuild does, and `WERROR` (default `-Werror`) is emptied by the spec; `VERSION` (0.2.0) becomes `PLM09_VERSION`, printed by `--version` |
| `install` | copies `plmc`, `sim6809` and `srec2flex` to `$(DESTDIR)$(BINDIR)` and their man pages to `$(DESTDIR)$(MANDIR)/man1`, and `runtime/` to `$(DESTDIR)$(DATADIR)/plm09/runtime`; `BINDIR` defaults to `$(PREFIX)/bin`, `MANDIR` to `$(PREFIX)/share/man`, `DATADIR` to `$(PREFIX)/share`, `PREFIX` to `/usr/local` |
| `rpm`, `srpm` | `git archive` of HEAD to `build/rpm/SOURCES/plm09-$(VERSION).tar.gz`, and `plm09-$(VERSION)-datadir.patch` (a copy of the tarball's README, plmc.md, sim6809.md, plmc.1 and srec2flex.1 converted by `tools/rpmdocs.py $(rpm --eval %{_datadir})`, diffed against the original), then (for `rpm`, after removing earlier builds' `RPMS` and `SRPMS`) `rpmbuild -ba`/`-bs plm09.spec` with `_topdir` `build/rpm`; the spec applies the patch, checks it names `%{_datadir}/plm09/runtime`, rebuilds the PDFs (`make -B docs`, hence its pandoc and TeX BuildRequires), and `%check` runs sim, man, lex, parse, err, run and fuzz |
| `test` | runs the suites sim, man, lex, parse, err, run, coco, xroar, os9, nitros9, flex, dragonflex, simon6809, kswichit, searle6809, fuzz |
| `sim-test`, `man-test`, `lex-test`, `parse-test`, `err-test`, `run-test`, `coco-test`, `xroar-test`, `nitros9-test` | one suite each |
| `os9-test OS9N=25` | the os9 suite, with OS9N fuzz seeds |
| `flex-test FLEXN=25` | the flex suite, with FLEXN fuzz seeds |
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
- Labels and GOTO (manual 5.3, 9.1–9.3):
  - Any executable statement may carry labels (`A: B: stmt`).
  - A label is implicitly declared in the smallest enclosing block. Every DO block, of any kind, and every procedure is a block; an IF isn't.
  - Labels may be used before their definition.
  - A label on a DO statement belongs to the enclosing block.
  - A GOTO may target any visible label, so not one inside a nested block. Leaving a procedure is allowed only to a label at the outer level of the main program.
- BASED variables (manual 3.6.3):
  - `X BASED P` must come right after the name, and the base must be an already declared ADDRESS scalar that isn't BASED.
  - A BASED variable has no storage; every reference uses P's value at that moment.
  - BASED arrays are allowed. Parameters and iterative DO indexes can't be BASED.
- The dot operator (manual 4.1.3) gives an ADDRESS:
  - `.X`, `.A(i)`, and `.A` (element 0)
  - for a BASED variable, the base's value (plus the element offset)
  - `.P` for an outer-level procedure written without arguments
  - `.(constant list)` for numbers 0–255 and strings, stored with the code
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
  - `LENGTH(A)`/`LAST(A)` are folded to constants, BYTE if 255 or less and ADDRESS otherwise (manual 11.1.2), unless the name is declared by the user. `SIZE(X)` is folded to an ADDRESS constant. Until the structures change they were always ADDRESS, which deviated from the manual.
  - `HIGH`, `LOW`, `DOUBLE`, `SHL`, `SHR`, `ROL`, `ROR` (manual 11.1.3–11.1.4), also only where the name isn't declared, become `E_BUILTIN` (`check_value_builtin`) and are compiled inline by `gen_builtin`, so no hooks or runtime routines are involved. A count of 0 (undefined in PL/M-80) leaves the pattern unchanged.
- INITIAL and DATA (manual 6.2.9):
  - Value lists (numbers, strings of any length, location reference ± numbers) fill a declaration's scalars in storage order, across factored names. They may be shorter than the declaration but not longer.
  - `(*)` takes the dimension from the list.
  - INITIAL is allowed only at the outer level of the main program; DATA anywhere, and it can't be assigned.
  - BASED variables and parameters can't be initialized.
- Structures (manual 3.5, 6.2.7):
  - `STRUCTURE (m [(dim)] BYTE|ADDRESS, ...)` is a type, usable with dimensions, factoring and BASED.
  - Members can't be factored, BASED or structures, and are laid out contiguously in order.
  - References are `X.M`, `X(i).M`, `X.M(j)` and `X(i).M(j)`.
  - Whole structures, or elements of structure arrays, can be used only as operands of `.` and the built-ins, which also allow the `LIST.M` shorthand.
  - Structures can't be parameters or iterative DO indexes.
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
- `PLUS`/`MINUS` follow the `+ -` typing and add/subtract the carry (borrow); `SCL`/`SCR` rotate through it, `DEC` uses it and the half carry, and `CARRY`/`ZERO`/`SIGN`/`PARITY` read the flags. The 8080 compiler uses the CPU flags, which the manual calls unpredictable; plmc defines them as those of the last `+ - PLUS MINUS SCL SCR DEC` executed and keeps them in memory, as far as the program needs them: the carry (and, for BYTE operations when DEC is used, the half carry) in `RT_CC`, by `ROL` (or `TFR CC,A`/`STA`); the result in `RT_RES`. PLUS, MINUS, SCL and SCR get the carry back with `LSR`; DEC puts carry and half carry into CC (leaving I and F) before `DAA`. Interrupt entry code saves and restores them.
- **No I/O ports:** PL/M-80's `INPUT(n)`/`OUTPUT(n)` (the 8080's `IN`/`OUT`) have no 6809 counterpart, so they are not provided: sema (`lookup()`) reports an undeclared `INPUT` or `OUTPUT` as not supported, pointing to a BYTE variable AT the device's address, and a program may declare the names for itself. (In 0.1.0 they were mapped to the bytes at `RT_PORTS + n`, an address from the hooks.)
- **TIME** is `RT_TIME`, a loop of 8 × `RT_TIMEK` + 8 cycles per 100 µs, from the hooks' `RT_CYCLES100`. **MOVE** uses `RT_COPY`. **MEMORY** is the label `RT_MEMORY` (after everything, or `--memory` bytes of an OS-9 data area). **STACKPTR** is `TFR S,D` / `TFR D,S`.
- Assigning ADDRESS to BYTE keeps the low byte. BYTE to ADDRESS zero-extends. Arguments and RETURN values are converted to the declared type.
- Division by zero gives a quotient of 0FFFFH and a remainder equal to the dividend. This is documented as something not to rely on.
- The order in which operands are evaluated is unspecified. The code generator evaluates the right operand first, except when it can be used directly as an instruction operand.

**Procedures** use static storage, as in classic PL/M, unless they are REENTRANT:
- Parameters and locals get fixed RAM slots, so every access is a plain extended-address access.
- Nested procedures can reach outer variables directly.
- Sema rejects recursion through procedures that aren't REENTRANT: for each one, `check_recursion` searches the call graph for a path back to it and reports the cycle.
- A typed procedure whose last statement can fall through (not RETURN, HALT, GOTO, or an IF/ELSE or DO block ending in them) gets a warning.
- **REENTRANT** procedures (outer level only, no nested procedures, not EXTERNAL) keep parameters and locals in a stack frame addressed through Y:
  - Prologue `PSHS Y`, `LEAS -n,S`, `TFR S,Y`; epilogue `LEAS n,Y`, `PULS Y,PC`. Callers push the arguments in their declared widths and remove them with `LEAS`.
  - Sema's `layout_frame()` assigns the offsets after the body is checked (variables from 0, arguments above the saved Y and return address); codegen emits them as `EQU`s before the procedure, and `var_ref()` adds `,Y`.
  - Y must survive calls: ordinary procedures and the runtime don't use it, and EXTERNAL calls save it when the program has any REENTRANT procedure.
  - `check_block` predeclares the main program's REENTRANT procedures (`Symbol.pending`) so REENTRANT procedures can call them before their declaration, as the manual allows.
- `Symbol.dim` holds array sizes. `Symbol.storage` is `ST_FRAME` for the variables of REENTRANT procedures (Y-relative).

**LITERALLY in the parser.** Substitution happens in the parser's token buffer, so sema and codegen never see literals.
- **Lexer:** strings are `T_STRING` tokens. The parser turns one- and two-character strings into constants, and `lex_tokens()` tokenizes a literal's text, located at the place it is used.
- **Scopes:** the parser keeps its own scope stack (`LitScope`) of declared names, pushed for the program, procedures and simple DO blocks.
- **Lazy expansion:** `peek_at(k)` expands positions 0..k, replacing a visible literal name by its tokens, which may expand in turn. Expansion depth is counted per token.
- **Raw names:** names being defined (DECLARE items, procedure and parameter names, labels, the program name, END labels) are read raw, by `next_raw_ident()` / `raw_type()`, and never expanded. This is what lets `DCL TWO BYTE` in an inner block hide an outer literal `TWO`.
- **Mixed lookahead:** `NAME: PROC` works with `PROC LITERALLY 'PROCEDURE'`, because the name and ':' are checked raw and only the third token is expanded.

**Rejected features.** Every unimplemented PL/M feature produces "... is not supported" rather than being misparsed:
- PUBLIC (except on labels)
- `:=`

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

**Labels and GOTO:**
- **Parser:** a statement's labels are read raw into `Stmt.labels` (each an `E_NAME`). Any of a DO's labels can close it with `END`. Literal scopes are pushed for every DO block.
- **Sema:**
  - Every DO body gets its own `Scope`.
  - `check_block()` first runs `collect_labels()` over the block's statements, descending into IF branches but not into nested blocks, so labels exist before the block's procedure bodies and statements are checked.
  - Labels are `SYM_LABEL` symbols with `owner` = the enclosing procedure; their assembler names are `G_X` / `Gn_X`.
  - `check_goto()` resolves the target through normal scope lookup, so labels in nested blocks are invisible. If the label's owner differs from the current procedure, it must be in scope 0 (the main program's outer level). Such a jump sets `Stmt.goto_out` and `sema_goto_out`.
  - A label/declaration clash is reported at the label.
  - LABEL declarations are entered first (`declare_label`, `Symbol.label_declared`); `collect_labels()` binds a definition at the outer level of the same block to the declared symbol instead of defining a new one. After the block is checked, a declared label that is used (or PUBLIC) but undefined is an error, and an unused one a warning.
  - `LABEL EXTERNAL` symbols take their exact name as assembler name and are never defined; GOTO treats them as main-program outer-level labels. `LABEL PUBLIC` symbols go in `sema_public_labels`, and codegen emits `NAME EQU G_NAME` for them.
- **Codegen:** labels are emitted before their statement's code, and a GOTO is `LBRA`. With `sema_goto_out`, `PLM_MAIN` starts with `STS RT_MAINS`, and a `goto_out` GOTO does `LDS RT_MAINS` first.

**INITIAL and DATA:**
- The parser attaches one `InitList` (items, plus a cursor) to all names of a declaration.
- Sema's `take_values()` walks each variable's scalars in storage order and builds `Symbol.init_vals`: numbers, or an `E_ADDR` plus an addend. A string supplies one character per BYTE and two per ADDRESS; `restricted()` checks the manual's restricted expressions. Leftover items are an error after the last name of the list.
- Codegen in absolute mode: INITIAL becomes `FCB`/`FDB` in the data section and DATA the same among the code, with addresses as label expressions (`addr_text`).
- Codegen with `--os9`:
  - DATA is in the module, padded with `ZMB`, and reached as `,PCR` (`var_ref` → `pic_suffix()` checks `Symbol.in_code`). It may not contain addresses.
  - INITIAL gets a module template, which a `PLM_MAIN` prologue copies with `RT_COPY` (assembled only when `RT_NEED_COPY` is defined). The prologue then computes each address with `gen_addr` and stores it.
- Constant lists are emitted last, because initial values can register them.

**AT:**
- The parser attaches one `AtSpec` to all names of a declaration; sema's `place_at()` evaluates it once with `restricted()` and gives each name the next offset (`Symbol.at_off`), after INITIAL so that `(*)` dimensions are known.
- A number makes the variable `absolute` (labels `M_X`); a location makes an alias, which inherits the target's `absolute` and `in_code`.
- Codegen emits an `EQU` instead of storage. INITIAL AT is `ORG` to the address, `FCB`/`FDB`, and `ORG` back; in `--os9` mode it goes through the same `RT_COPY` prologue as other INITIAL variables.
- The os9 suite builds the `PLMCARGS` run tests too (including `atabs`, whose fixed addresses can't run under memory protection) and checks their code, and `os9_check_pic` rejects an `M_X` operand with an index register.

**Structures:**
- The parser builds `StructType`/`Member` (`parse_members`); `member_suffix()` turns `NAME[(i)].M[(j)]` into `E_MEMBER`.
- Sema lays out the members (`declare_var`) and checks references (`check_member`).
- The flags `whole_ok` (operand of `.` or a built-in) and `shorthand_ok` (built-ins only) control where whole structures and `LIST.M` are allowed. `check_sub()` clears them for subscripts.
- In codegen, `const_offset()` folds constant references into a byte offset, so `fixed_location`/`based_location` work as for arrays. `gen_elem_offset()` sums the computed parts in D using `scale_d()` (shifts, or `RT_MUL16` for other sizes), then loads the base into X.

**BASED variables and `.`:**
- `fixed_location()` excludes BASED variables. `based_location()` covers BASED scalars and constant-subscript elements, which are reached as `k,X` after `gen_base_x()` has loaded X with the base's value (`LDX P` / `LDX P,U`). Loading X doesn't disturb D, so stores need no push.
- `gen_elem_offset()` also uses `gen_base_x()`, so computed subscripts on BASED arrays work unchanged.
- `gen_addr()` builds the address in D:
  - `LDD #V+k`, or `LEAX V+k,U` / `TFR X,D` when position-independent
  - for BASED variables, the base's value plus k
  - for computed subscripts, `LEAX D,X` / `TFR X,D`
  - for procedures, `#P` or `LEAX P,PCR`
  - for constant lists, the label of `FCB` data emitted after the procedures (the `Pool` entries)

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
- variables AT a fixed address: `M_X` / `Mn_X`
- procedures `P_X` / `Pn_X`
- statement labels `G_X` / `Gn_X`
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
7. with `--vectors`, the vectors of INTERRUPT procedures and `ORG $FFFE` / `FDB RT_START`
8. `END RT_START`

**The hooks contract.** A `plm_hooks.asm` must define:
- `RT_STACKTOP`
- `PLM_EXIT` (exit status in B; must not return)
- `PLM_HALT`
- `PLM_SETINT` (B = interrupt number, X = entry routine), only for programs with INTERRUPT procedures
- `RT_CYCLES100` (cycles in 100 µs), only for programs using TIME
- one label per EXTERNAL procedure the program uses

Other details:
- Mnemonics are always emitted with a leading tab.
- No `@` local labels.
- `HALT` becomes `JMP PLM_HALT` (preceded by `ANDCC #$AF` when the program has INTERRUPT procedures).
- INTERRUPT procedures: `PLM_MAIN` installs each through `PLM_SETINT`; the entry routines `RT_INTn` (after the procedures) set DP = 0, call the procedure and `RTI` (FIRQ's also save the registers the CPU doesn't). ENABLE/DISABLE are `ANDCC #$AF`/`ORCC #$50`; a CALL of an interrupt procedure is followed by `ANDCC #$AF`.

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
All five provide `PUTCH`, `PUTDEC`, `GETCH` and `EXIT`.

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
- **flex** (FLEX 9, any machine; only the DOS jump table at $CD00 is used):
  - PUTCH uses PUTCHR ($CD18), sending LF as PCRLF ($CD24); PUTDEC uses OUTDEC ($CD39) on the number pushed on the stack, then PCRLF
  - GETCH uses GETCHR ($CD15)
  - EXIT, end of program and HALT jump to WARMS ($CD03); FLEX has no exit status, B is passed on (sim6809 --flex uses it)
  - stack at `RT_STACKTOP` = $C700, the top of the utility command area (the top of user RAM is where EXEC and spoolers park themselves, lowering MEMEND); `RT_CYCLES100` (100) is machine-dependent, both overridable with `-D`
  - no PLM_SETINT (interrupt vectors depend on the machine)
  - a FLEX binary is made from lwasm's S-records by `srec2flex` ($02 records of up to 252 bytes, $16 transfer address from S9), since lwasm has no FLEX format
- **simon6809** (Erturk Kocalar's Simon6809 board, BUGGY monitor): console through the monitor's RAM I/O vectors (getchar $001B, putchar $001E, putcr $0027; FT245RL at $D000 underneath); exit, HALT and EXIT with SWI (a breakpoint to the monitor, which shows B); PLM_SETINT patches the RAM JMP vectors ($0009 IRQ, $0006 FIRQ, $000F NMI) and PLM_EXIT restores them; programs at $0400, stack $8000; RT_CYCLES100 100.
- **kswichit** (Wichit Sirichote's 6809 Microprocessor Kit): the 6850 ACIA at $A000 used directly (the monitor's routine addresses differ between versions); SWI back to the monitor; PLM_SETINT patches the RAM vectors ($7FF0 IRQ, $7FF6 FIRQ, $7FF3 NMI); programs at $0200, stack $7000 (below the monitor's $7000-$7FFF); RT_CYCLES100 123 (1.2288 MHz).
- **searle6809** (Grant Searle's 6809 SBC, Jeff Tranter's combined ASSIST09+BASIC ROM or Grant's BASIC ROM): the ACIA at $A000 used directly; the program is a subroutine of the monitor (C) or BASIC (EXEC) and returns with LDS RT_ENTRYS / RTS; stack $6FD0 (below ASSIST09's RAM), programs at $1000; PLM_SETINT follows the CPU vector: ASSIST09's `JMP [n,PCR]` (store the handler's address in the word it reads, 2 bytes) or a RAM JMP (BASIC's $00A4/$00A7/$00AA, 3 bytes); PLM_EXIT restores exactly the bytes changed (ASSIST09's IRQ and FIRQ words are adjacent, so restoring 3 bytes clobbered one, which the suite's vector check caught); RT_CYCLES100 184.
- **example:**
  - MC6850 ACIA template (`ACIA_BASE`, default $A000; `RT_STACKTOP`, default $8000)
  - both can be overridden with lwasm `-D`
  - PUTDEC is built on RT_DIV16
  - exit and HALT spin forever
  - tests only check that it assembles

## Simulator (sim/)
**CPU**
- The full documented MC6809 instruction set and all addressing modes. No cycle accuracy.
- Interrupts: IRQ and FIRQ lines (level-triggered) and NMI (edge), asserted and released through MMIO, plus an instruction-count timer. `cpu_interrupt()` is called before each step; it stacks and vectors as the MC6809 does. CWAI and SYNC wait for an interrupt (the wait counts against `-n`).
- Reset state: all registers 0, including S, and CC=$50.
- Memory starts filled with $A5 (`--zero` for 0), so tests catch use of uninitialized variables.
- Where the datasheet leaves results undefined (H after subtract, V after DAA/SEX, mixed-size TFR/EXG), the choices are documented in `sim6809.md`.
- Undocumented opcodes, reserved indexed postbytes, and SWI/SWI2/SWI3 stop the simulator with a register dump and exit status 125.
- The instruction limit (`-n`, default 100,000,000) stops it with exit status 124.
- Usage and load errors give exit status 2.
- `-t` prints a trace with disassembly to stderr.
- `--io BASE` moves the MMIO block, and `--entry ADDR` overrides the S9 entry address.
- S-records: S1/S9 are loaded, S0/S5 ignored, checksums verified. If several S9 records are present, the last one wins, which lets two files be combined.
- Loading: S-records and Intel HEX (`:` lines, types 00/01) in the same file; `--reset` starts at [$FFFE]; `--rom ADDR` makes [ADDR, $10000) ignore writes (`Cpu.rom` with the `ro_lo`/`ro_hi` range; the I/O block still works), as real ROM does — Grant's BASIC sizes RAM by writing to it.
- Board devices: `--swi` executes SWI/SWI2/SWI3 through their vectors; `--acia ADDR` is an MC6850 (TDRE always; RDRF only after 32 status reads since the last data access — monitors like ASSIST09 poll for a key between output characters and would swallow input given earlier; data from stdin, 0 at its end, or with `--stop-at-end` a status poll after the end stops the run); `--ft245 ADDR` is an FT245RL whose data-available line drives FIRQ (`cpu_interrupt()` peeks stdin), and waiting in SYNC/CWAI after the end of stdin ends the run with status 0.
- `--flex` loads a FLEX binary and emulates the DOS entry points through `Cpu.pc_hook` (`flex_call()`, called before each instruction): the terminal routines, OUTDEC/OUTHEX/OUTADR, CLASS, ADDBX, and WARMS, which stops with B as the exit status. Writes to $C700–$DFFF (`Cpu.ro_lo`/`ro_hi`) and code run there other than at an entry point stop the program.

**MMIO** (base $FF00, configurable)
| Address | Access | Meaning |
|---|---|---|
| +0 | write | put a character |
| +1 | read | get a character (0 at end of input) |
| +2 | write | exit with that status |
| +4/+5 | write | print the 16-bit value in decimal; `STD $FF04` is enough |
| +6 | write / read | assert interrupt lines (1 IRQ, 2 FIRQ, 4 NMI) / the asserted lines |
| +7 | write | release lines |
| +8/+9 | write | timer period in instructions, started by the write to +9 (0 stops it) |
| +10 | write | the lines the timer asserts (default IRQ) |

Instruction fetches bypass MMIO; all other accesses go through it.

**Checking the simulator itself**
1. Self-checking assembly tests for flags, branches, indexed modes, PSH/PUL, TFR/EXG, RTI, DAA, MUL and interrupts (IRQ/FIRQ/NMI, timer, CWAI, SYNC). They use `sim/tests/harness.inc`, with expected values worked out by hand from the datasheet at the edge cases.
2. An instruction-length check. `gen_len_ops.py` generates every opcode in every addressing mode as straight-line code (about 11,000 instructions). The traced (address, bytes) pairs must match the lwasm listing (`--list=`).
3. Mutation checks during development. Injected bugs, such as wrong flags or a mis-decoded offset, were confirmed to fail the tests.

## Test suites
| Suite | Contents |
|---|---|
| sim | the checks above, plus the runtime routines (`rt_muldiv`, `rt_hooks`) |
| man | each `docs/man/*.1` formats without warnings (`groff -man -ww -z`) and names every option its program's `--help` lists |
| lex, parse | `--tokens` / `--ast` golden files |
| err | programs that must fail; the `.err` file holds the expected message, with position |
| run | complete programs in sim6809, covering every operator and conversion rule, control flow, input, procedures, HALT, `--org`/`--vectors`, and an assembly check with the example hooks |
| fuzz | `exprgen` seeded random programs against an independent C model of the language rules. They include pointer statements (`P = .BA(k)` with reads and writes through BASED `PB`/`PW` and the BASED arrays `PBA`/`PWA`). They include elements of `BA(4)`/`WA(4)` (constant or `(expr) AND 3` subscripts), assignments to elements, iterative DO loops over random bounds and steps (the model follows the manual's four steps and rejects loops of over 300 passes), and `DO CASE (expr) AND 3`. A failing program is kept as `build/tests/fuzz-fail-SEED.plm` |
| coco | the CoCo hooks in sim6809 with `tests/coco/fakerom.asm`, a stand-in Color BASIC ROM loaded last so its fake EXEC is the entry point. It checks that the program returns with BASIC's S intact, and its CHROUT clobbers Y |
| xroar | the CoCo programs that have a `.screen` file, run on XRoar's `cocous` (CoCo 2, Disk Extended BASIC 1.1); skipped if xroar, `decb` or the ROMs are missing |
| os9 | every run program (except the `--org`/`--vectors` one) and 25 fuzz programs built with `--os9` and run by sim6809 `--os9` at two module/data placements (`$2000`/`$8000` and `$3123`/`$9E00`), with the text check for absolute references, plus `os9 ident` when toolshed is present |
| simon6809 | the run programs (and `tests/simon6809/*.plm`) on the Simon6809's real monitor ROM (`$SIMON_ROM`) in sim6809 `--ft245 0xD000 --swi --io 0xD800`: typed in as S1/S9 records, run with G400, output cut at the monitor's register display, whose B is the exit status; skipped without the ROM |
| searle6809 | the run programs (and `tests/searle6809/*.plm`) on Grant Searle's SBC in sim6809 `--acia 0xA000 --stop-at-end --swi --io 0x9000 --rom 0xC000 --reset`, twice: under Jeff Tranter's combined ROM (`$SEARLE_ROM`; ASSIST09 `L`, `C 1000`, output up to the register display, B = exit status) and Grant's BASIC ROM (`$SEARLE_BASIC`, Intel HEX; preloaded with the program, `CLEAR 200,&HFFF` / `EXEC &H1000`, output up to `OK`). With a `.vectors` file, the interrupt vectors are dumped before and after and must match |
| kswichit | the run programs (and `tests/kswichit/*.plm`) with the kit's hooks in sim6809 `--acia 0xA000 --swi`, with `tests/kswichit/fakemon.asm` standing in for the keypad-driven monitor (its reset's RAM vectors; SWI checks they were restored and exits with B) |
| dragonflex | the run programs without PLMCARGS or input on real FLEX (Compusense FLEX 1.3.2 for the Dragon 64, `$FLEX_DISK`) under XRoar, outputs captured from the printer port; skipped if XRoar or the disk is missing |
| flex | every run program without PLMCARGS and 25 fuzz programs, built with the FLEX hooks, converted by `srec2flex` and run by `sim6809 --flex`; FLEX's memory is protected |
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

How the dragonflex suite works (each point was found the hard way):
- **The image:** Compusense FLEX System v1.3.2 for the Dragon 64, from the Dragon Archive (worldofdragon.org), SHA-1 `c812587d…`, the same as MAME's `dragon_flex` software list. It is a VDK (40 tracks × 18 sectors of 256 bytes) whose banner says FLEX 1.2.0. `tools/flexdisk.py` reads and writes FLEX file systems: the SIR at track 0 sector 3, the directory from sector 5, sectors linked through their first two bytes, and the free chain.
- **Booting:** XRoar's `dragon64` with DragonDOS (`-type '\rBOOT\r'`; the RETURN dismisses the Dragon 64's PRESS ANY KEY). DragonDOS runs the "OS" boot code in track 0 sector 3, which loads FLEX from a chain at track 1 sector 1, not from FLEX.SYS.
- **The date prompt:** FLEX's cold start calls a date routine ($CAAC, from $CA02) that reads a line with FLEX's own keyboard driver. XRoar's `-type` intercepts the BASIC ROM's keyboard routine, so it can't answer; XRoar's GDB target could, but only with rate limiting (with `-no-ratelimit` the emulation thread starves the GDB thread). The suite instead patches the boot chain's `JSR $CAAC` to NOPs (`flexdisk.py patch @1/1 CA02 BDCAAC 121212`, which checks the old bytes).
- **The screen** is a 256×192 graphics screen at $E600, which isn't practical to read. Output goes to the printer instead: `P,command` (PRINT.SYS drives the parallel port), which XRoar appends to a file with `-lp-file`.
- **Sequencing:** `O` and `P` pass the rest of the command line to the command they run, so commands can't share a line. STARTUP `EXEC`s a command file instead. `O` (output to a file) can't run from `EXEC` ("THE REQUESTED FILE IS IN USE"); `P` can. EXEC moves itself to the top of user memory and lowers the memory end ($CC2B), which is why the hooks' stack is in the utility command area and MARK and DONE run there ($C100, $C600).
- **Input:** `I,file,command` works with the hooks' FMS path in GETCH (end of file gives 0), but `I` resets the output vectors, so `P` no longer prints; programs with a `.in` file are skipped.
- **Debugging:** an XRoar snapshot holds the 64K RAM at a small, varying offset (about 530 bytes); find it from a known byte pattern at a known address, not from the jump table, which also has a copy in low RAM after booting.

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
- BASED variables and the dot operator: the run test `based`, pointer statements in the fuzz generator, a parse golden and error tests
- INITIAL and DATA, `(*)`, and strings of any length in value lists: the run test `initial`, INITIAL set-up in the fuzz generator, a parse golden and error tests
- structures and `SIZE`, and BYTE/ADDRESS typing for `LENGTH`/`LAST`: the run test `structs`, structure members in the fuzz generator, a parse golden and error tests
- labels and GOTO: the run test `goto` (including 10000 GOTOs out of three nested calls, which crashes if the stack isn't reset), a parse golden and error tests
- AT: the run tests `at` (aliases) and `atabs` (fixed addresses, sim6809's I/O ports), a parse golden and error tests
- INTERRUPT, ENABLE and DISABLE, with IRQ/FIRQ/NMI and a timer in sim6809: the sim test `interrupts`, the run test `interrupt`, the coco tests `interrupt` (CoCo hooks against the fake ROM) and `vsync` (60 Hz IRQs on XRoar), a parse golden and error tests
- LABEL declarations: the run test `labels` (declared labels in the program, a procedure and a DO block, an inner implicit label hiding a declared one, a PUBLIC label, and an EXTERNAL label naming the hooks' `EXIT`), a parse golden and error tests
- PLUS and MINUS: the run test `carry` (32- and 24-bit arithmetic, what changes the carry, evaluation order), 32-bit counting under interrupts in `interrupt`, carry statements in the fuzz generator, a parse golden and error tests
- HIGH, LOW, DOUBLE, SHL, SHR, ROL, ROR: the run test `builtins`, the coco test `builtins` (also on XRoar), built-ins in the fuzz generator's expressions, a parse golden and error tests
- the other built-ins and predeclared variables: the run tests `flags` (BCD arithmetic with DEC, SCL/SCR, CARRY/ZERO/SIGN/PARITY), `memory` (MOVE, MEMORY, STACKPTR, TIME; in the os9 suites with `--memory 128`), `atio` (an array AT the simulator's I/O block, in place of INPUT/OUTPUT) and `ioname` (INPUT and OUTPUT declared as ordinary names), the XRoar test `time` (40 × TIME(250) is 59 field-sync ticks, predicted 0.983 s), flag statements in the fuzz generator, a parse golden and error tests
- Grant Searle's 6809 SBC: its hooks, sim6809 Intel HEX loading, `--reset`, `--rom`, ACIA input pacing and `--stop-at-end`, and the `searle6809` suite on both real ROMs
- the Simon6809 board and the 6809 Microprocessor Kit: their hooks, sim6809 `--swi`/`--acia`/`--ft245` (sim test `devices`), the `simon6809` and `kswichit` suites with an interrupt test each
- REENTRANT: the run test `reentrant` (the manual's SUM$ARRAY, indirect recursion through a forward call, local arrays and structures, Ackermann, a GOTO out of 20 activations), half the fuzz programs running inside a REENTRANT procedure, a parse golden and error tests
- INPUT and OUTPUT removed in 0.2.0: the 6809 has no I/O ports, so the 8080 port I/O was dropped (with `RT_PORTS` in the hooks) in favour of BYTE variables AT device addresses; the run tests `atio` and `ioname` and the error tests `input_unsupported`/`output_unsupported` replace `ports` and the port-number checks

## Known gaps
- **Language:** PUBLIC (other than on labels), `:=`, are not supported. TIME's timing is only checked on an emulated CoCo; sim6809 has no cycle counts. Arrays are one-dimensional and can't be parameters.
- **Code size:** statement control flow always uses long branches.
- **CoCo testing:** the CoCo hooks are tested only on an emulated CoCo 2 with Disk Extended BASIC 1.1, not on a CoCo 3, other ROM versions or real hardware. `CLS` relies on the fixed ROM address $A928.
- **FLEX:** real FLEX is only Compusense FLEX for the Dragon 64 (emulated); the FMS path of GETCH (input from a file) was checked by hand with an XRoar trace, not by the suite.
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
- `man/*.1` when a program's options change

Then rebuild the PDFs with `make docs`.
