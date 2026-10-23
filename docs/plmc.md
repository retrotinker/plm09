---
title: "plmc"
subtitle: "A minimal PL/M compiler for the Motorola 6809 -- user manual"
author: "plm09 project"
date: "October 2026"
toc: true
toc-depth: 2
numbersections: true
geometry: margin=2.5cm
fontsize: 10pt
colorlinks: true
---

# Introduction

`plmc` compiles programs written in a small subset of PL/M-80, Intel's systems
programming language of the late 1970s, into assembly source for the Motorola
6809. Its output is meant for **lwasm**, the assembler in the lwtools package
(version 4.24 or later). lwasm then produces whatever binary format the target
needs: S-records, Disk BASIC `.BIN` files, raw images and so on.

The compiler is not tied to any particular machine. Everything that depends on
the hardware lives in a small **hooks file** that lwasm includes, chosen with an
include path. Three hooks files come with plmc:

| Hooks | Target |
|---|---|
| `runtime/sim/` | `sim6809`, the headless simulator used by the test suite |
| `runtime/coco/` | the Tandy Color Computer 1, 2 and 3 under Color BASIC |
| `runtime/example/` | a template for a board with an MC6850 ACIA serial port |

**Main features**

* BYTE (8-bit) and ADDRESS (16-bit) unsigned variables, with PL/M-80's rules
  for mixing the two types.
* All PL/M arithmetic, logical and relational operators, with PL/M precedence.
* `IF`/`THEN`/`ELSE`, `DO` blocks with their own declarations, `DO WHILE`
  loops, `HALT`.
* Procedures: typed and untyped, with parameters, nested inside each other, and
  `EXTERNAL` procedures written in assembly.
* Error messages that give the file, line and column. Every unsupported PL/M
  feature is rejected with a clear message rather than misread.
* Readable output: each source statement appears as a comment above the code
  generated for it.

# Building and quick start

`plmc` is plain C11. From the top of the plm09 tree:

```
make            # builds build/plmc and build/sim6809
make test       # runs every test suite
```

A first program:

```
SUM: DO;
PUTDEC: PROCEDURE (N) EXTERNAL; DECLARE N ADDRESS; END PUTDEC;
DECLARE I BYTE, TOTAL ADDRESS;
TOTAL = 0;
I = 1;
DO WHILE I <= 10;
    TOTAL = TOTAL + I;
    I = I + 1;
END;
CALL PUTDEC(TOTAL);
END SUM;
```

Compile it, assemble it, and run it in the simulator:

```
$ build/plmc -o sum.asm sum.plm
$ lwasm -9 -f srec -I runtime -I runtime/sim -o sum.s19 sum.asm
$ build/sim6809 sum.s19
55
```

`make run F=sum.plm` does all three steps. Always pass `-9` to lwasm (or
rely on the `PRAGMA 6809` that plmc emits). By default lwasm accepts the
Hitachi 6309's extra instructions, which a 6809 doesn't have.

# Command-line usage

```
plmc [options] file.plm
```

| Option | Meaning |
|---|---|
| `-o FILE` | Write the assembly to *FILE*. The default is standard output. |
| `--org ADDR` | Address where the program is loaded and starts. The default is `$0100`. |
| `--vectors` | Also emit `ORG $FFFE` / `FDB RT_START`, setting the 6809 reset vector, for programs that run from ROM or straight from reset. |
| `--tokens` | Print the token stream and stop. See [Inspecting the compiler's view]. |
| `--ast` | Print the syntax tree and stop. |
| `-h`, `--help` | Print a usage summary and exit. |

The `--org` address can be written as decimal (`8192`), `0x2000`, `$2000`
(quote it for the shell) or PL/M style `2000H`. It must fit in 16 bits.

**Exit status**

| Status | Meaning |
|---|---|
| 0 | Success. Any warnings are still printed. |
| 1 | Compilation error, or a file couldn't be read or written. If `-o` was given and writing failed, the partial output file is removed. |
| 2 | Bad usage, for example no input file or an unknown option. |

# The language

plmc accepts a **subset** of PL/M-80. Anything outside the subset is
rejected with an error (see [Unsupported features]), so a program that
compiles means what PL/M-80 says it means.

## Lexical rules

* **Case:** case doesn't matter. `Total`, `TOTAL` and `total` are the same name, and keywords may be written in any case.
* **The `$` character:** ignored inside names and numbers, so you can use it as a separator: `MAX$COUNT`, `1$000`, `0FF$FFH`.
* **Names:** start with a letter and continue with letters, digits and `$`.
  The underscore is **not** allowed (as in PL/M-80). This is what keeps
  compiler-generated labels from ever colliding with your names.
* **Comments:** `/* ... */`. They may span several lines but don't nest.
* **Numbers:** must start with a digit and fit in 16 bits (0 to 65535). An
  optional suffix gives the base:

  | Suffix | Base | Example | Value |
  |---|---|---|---|
  | none or `D` | decimal | `255`, `255D` | 255 |
  | `H` | hexadecimal | `0FFH` | 255 |
  | `O` or `Q` | octal | `377Q` | 255 |
  | `B` | binary | `1111$1111B` | 255 |

  A hexadecimal number has to start with a digit, so write `0FFH`, not `FFH`
  (`FFH` is a name).
* **Strings:** a one-character string such as `'A'` is a BYTE constant (65).
  A two-character string such as `'AB'` is an ADDRESS constant
  (`'A'`$\times$ 256 + `'B'`). Write a quote character as `''`, as in `''''`.
  Longer strings are not supported.
* **Reserved words:** all PL/M-80 reserved words, including ones for features
  plmc doesn't implement, so they can't be used as names: `ADDRESS AND BASED
  BY BYTE CALL CASE DATA DECLARE DISABLE DO ELSE ENABLE END EOF EXTERNAL GO
  GOTO HALT IF INITIAL INTERRUPT LABEL LITERALLY MINUS MOD NOT OR PLUS
  PROCEDURE PUBLIC REENTRANT RETURN STRUCTURE THEN TO WHILE XOR`.

## Program structure

A program is one labeled block:

```
NAME: DO;
    declarations
    statements
END NAME;
```

The name after `END` is optional, but if present it must match. The block may
optionally be followed by the keyword `EOF`. Within any block, **all
declarations must come before the first statement.**

## Declarations

```
DECLARE COUNT BYTE;
DECLARE (X, Y, Z) ADDRESS;
DECLARE FLAG BYTE, LIMIT ADDRESS, (LO, HI) BYTE;
```

* **Types:** `BYTE` is an unsigned 8-bit value (0 to 255) and `ADDRESS` an
  unsigned 16-bit value (0 to 65535).
* **Order and scope:** names must be declared before they are used. A name
  declared in an inner block or procedure hides the same name from an outer
  one. Declaring a name twice in the same block is an error.
* **Initial values:** variables have no initial value. They start with whatever
  is in memory, which is `$A5` in the simulator so that the mistake shows up.

## Procedures

```
NAME: PROCEDURE [(PARAM, ...)] [BYTE | ADDRESS] [EXTERNAL];
    declarations
    statements
END [NAME];
```

* **Parameters:** each one must be declared, with its type, in the
  procedure's own declarations.
* **Typed and untyped:**
  * A typed procedure (`BYTE` or `ADDRESS`) returns a value with
    `RETURN expression;` and is used inside expressions.
  * An untyped procedure is invoked with `CALL` and may use a bare `RETURN;`.
  * Using one kind where the other is expected is an error.
* **No arguments:** a typed procedure with no parameters is called just by
  writing its name: `X = NEXTCHAR;`.
* **Nesting:** procedures may be nested. An inner procedure can use the
  parameters and variables of the procedures around it, and of the program.
* **Argument conversion:** arguments are converted to the parameter's
  declared type, and `RETURN` values to the procedure's type. See
  [Types and conversions](#conversions).
* **Reaching `END`:** an untyped procedure just returns. A typed procedure
  that might reach its `END` without a `RETURN` gets a warning, and the value
  it returns is then undefined.
* **No recursion:** as in classic PL/M-80, parameters and local variables
  live at fixed memory locations, not on the stack. A procedure must not call
  itself, directly or through other procedures. plmc reports both cases:

  ```
  error: procedure A is recursive (A -> B -> A); recursion needs
         REENTRANT, which is not supported
  ```

### EXTERNAL procedures

An `EXTERNAL` procedure is written in assembly language, usually in the hooks
file. Its PL/M declaration gives only the parameters and their types:

```
PUTCH: PROCEDURE (C) EXTERNAL;
    DECLARE C BYTE;
END PUTCH;
```

The body may contain only the parameter declarations. In the assembly the
procedure is called by its exact PL/M name, so it must not be a 6809 register
name (`A`, `B`, `D`, `X`, `Y`, `U`, `S`, `PC`, `CC`, `DP`, `PCR`). The calling
convention is described under [Hooks].

## Statements

| Statement | Form |
|---|---|
| Assignment | `X = expression;` |
| Multiple assignment | `X, Y, Z = expression;` (evaluated once, converted for each target) |
| Call | `CALL NAME;` or `CALL NAME(arg, ...);` (untyped procedures only) |
| Conditional | `IF condition THEN statement; [ELSE statement;]` |
| Block | `[LABEL:] DO; declarations statements END [LABEL];` |
| Loop | `[LABEL:] DO WHILE condition; statements END [LABEL];` |
| Return | `RETURN;` or `RETURN expression;` (inside procedures only) |
| Halt | `HALT;` stops the program through the hooks' `PLM_HALT` |
| Empty | `;` |

* **ELSE:** an `ELSE` belongs to the nearest `IF` that doesn't already have
  one. To put several statements under `THEN` or `ELSE`, wrap them in
  `DO; ... END;`.
* **Labels:** only `DO` blocks may be labeled, and the label exists only so
  that `END` can be checked against it.
* **Loop blocks:** a `DO WHILE` block may not contain declarations.

## Expressions

Operators, from highest to lowest precedence:

| Precedence | Operators | Notes |
|---|---|---|
| 1 (highest) | `( )` | grouping |
| 2 | unary `-`, unary `+` | negation (two's complement) |
| 3 | `*` `/` `MOD` | multiply, divide, remainder |
| 4 | `+` `-` | add, subtract |
| 5 | `=` `<>` `<` `>` `<=` `>=` | comparisons |
| 6 | `NOT` | bitwise complement |
| 7 | `AND` | bitwise and |
| 8 (lowest) | `OR` `XOR` | bitwise or, exclusive or |

* **Grouping:** operators of equal precedence group left to right, so
  `A - B - C` is `(A - B) - C`. Comparisons do too: `A < B = C` compares the
  result of `A < B` with `C`.
* **Low precedence of `NOT`:** `NOT` binds more loosely than the comparisons,
  so `NOT A < B` means `NOT (A < B)`.
* **Evaluation order:** the order in which operands are evaluated is not
  defined, as in PL/M. Avoid expressions where a procedure call changes a
  variable used elsewhere in the same expression.

## Types and conversions {#conversions}

The type of every expression is worked out at compile time:

| Expression | Result type |
|---|---|
| constant 0--255 | BYTE |
| constant 256--65535, or a two-character string | ADDRESS |
| variable | its declared type |
| typed procedure call | the procedure's type |
| `a + b`, `a - b`, `a MOD b`, `a AND b`, `a OR b`, `a XOR b` | BYTE if *both* operands are BYTE, otherwise ADDRESS |
| `a * b`, `a / b` | always ADDRESS |
| `a = b`, `a < b`, ... | BYTE: `0FFH` for true, `0` for false |
| `-a`, `NOT a` | the type of `a` |

Rules that follow from this table:

* **BYTE arithmetic wraps at 8 bits, before any widening.** If `B` is a BYTE
  holding 200, `B + 100` is 44, *even when the result is assigned to an
  ADDRESS*. When you need the full result, make one operand an ADDRESS, for
  example by copying `B` into an ADDRESS variable first: `W = B; W = W + 100;`.
* **A constant above 255 makes the operation 16-bit.** `B + 300` is ADDRESS
  arithmetic.
* **Multiplication never wraps at 8 bits.** `200 * 200` is 40000.
* **Unsigned only.** All arithmetic and comparisons are unsigned. `0FFFFH > 1`
  is true.
* **Assignment converts:** an ADDRESS stored in a BYTE keeps its low byte, and
  a BYTE stored in an ADDRESS is zero-extended. The same applies to arguments
  and `RETURN` values.
* **Division by zero** gives a quotient of `0FFFFH` and a remainder equal to
  the dividend. Programs should not rely on this.

## Conditions

`IF` and `DO WHILE` test **only bit 0** of their condition. A comparison gives
`0FFH` or `0`, so this works as expected for comparisons and for `AND`, `OR`
and `NOT` applied to them. For other values it means `IF 2` is false and
`IF 3` is true. `AND` and `OR` always evaluate both operands; they do not
short-circuit.

## Unsupported features

These parts of PL/M-80 are recognized and rejected with an error:

| Feature | Error message |
|---|---|
| arrays, `DECLARE X(10) BYTE`, `X(I) = ...` | `arrays is not supported` / `subscripted assignment (arrays) is not supported` |
| `BASED` variables, `STRUCTURE`, `LABEL` declarations | `... is not supported` |
| `LITERALLY` macros | `LITERALLY is not supported` |
| `INITIAL` and `DATA` | `initialized data is not supported` |
| the address-of operator `.X` | `the address-of operator '.' is not supported` |
| iterative `DO I = 1 TO 10` and `DO CASE` | `iterative DO is not supported`, `DO CASE is not supported` |
| `GOTO`, `GO TO`, labels on other statements | `GOTO is not supported`, ... |
| `REENTRANT`, `INTERRUPT`, `PUBLIC` | `... is not supported` |
| `PLUS` and `MINUS` (add and subtract with carry) | `PLUS/MINUS (add/subtract with carry) is not supported` |
| embedded assignment `:=` | `embedded assignment ':=' is not supported` |
| `ENABLE`, `DISABLE` | `ENABLE/DISABLE is not supported` |
| strings of three or more characters | `strings longer than 2 characters are not supported` |

Built-in procedures such as `HIGH`, `LOW`, `SHL`, `SHR`, `ROL`, `ROR`,
`LENGTH` and `LAST` are not predefined. Using one gives
`... is not declared`, unless you write it yourself as a procedure or an
`EXTERNAL`.

# Diagnostics

Errors and warnings go to standard error in the form
`file:line:column: error: message`:

```
$ build/plmc e1.plm
e1.plm:3:5: error: Y is not declared
```

Compilation stops at the first error. Warnings don't stop compilation and
don't change the exit status. Columns count from 1, and tabs advance to the
next multiple of 8 plus 1.

| Message | Cause |
|---|---|
| `expected X, found Y` | syntax error; *X* is what the parser needed at this point |
| `X is not declared` | name used before (or without) its declaration |
| `X is already declared at line N` | second declaration in the same scope |
| `parameter X of P is not declared` | parameter without a `DECLARE` in the procedure |
| `procedure P expects N arguments, got M` | wrong number of arguments |
| `X is not a procedure` | `CALL X` or `X(...)` where X is a variable |
| `typed procedure F must be used in an expression, not CALLed` | `CALL` of a BYTE or ADDRESS procedure |
| `untyped procedure P cannot be used in an expression` | value taken from an untyped procedure |
| `cannot assign to procedure P` | procedure name on the left of `=` |
| `RETURN outside a procedure` | `RETURN` in the main program |
| `untyped procedure P cannot return a value` / `RETURN in typed procedure F needs a value` | `RETURN` does not match the procedure's type |
| `procedure P is recursive (...)` | direct or indirect recursion |
| `END X does not match ...` | label after `END` differs from the block or procedure |
| `declarations must come before statements` | `DECLARE` or procedure after the first statement |
| `declarations are only allowed in procedures and simple DO blocks` | `DECLARE` inside `DO WHILE` |
| `an EXTERNAL procedure may only declare its parameters` / `... cannot have statements` | body of an `EXTERNAL` procedure |
| `EXTERNAL name X is also a 6809 register name` | external called `A`, `X`, `PC`, ... |
| `invalid digit`, `invalid number`, `number ... does not fit in 16 bits` | bad numeric constant |
| `invalid character`, `unterminated comment`, `unterminated string`, `empty string` | lexical errors |
| warning: `typed procedure F may reach its END without RETURN` | last statement is not `RETURN` or `HALT` |

# Hooks

## How targets are selected

The assembly plmc writes always ends with:

```
        INCLUDE "plm_rt.asm"
        INCLUDE "plm_hooks.asm"
```

lwasm searches its `-I` directories for these files. `plm_rt.asm` and
`plm_crt0.asm` come from `runtime/`. Which `plm_hooks.asm` is used depends on
which hooks directory is on the include path, so **the same `.asm` file can
be assembled for different machines** without recompiling:

```
lwasm -9 -f srec -I runtime -I runtime/sim  -o prog.s19 prog.asm   # simulator
lwasm -9 -f decb -I runtime -I runtime/coco -o PROG.BIN prog.asm   # CoCo
```

## What a hooks file must define

| Symbol | Purpose |
|---|---|
| `RT_STACKTOP` | Initial stack pointer; the stack grows down from here. The value 0 means "keep the caller's stack". |
| `PLM_EXIT` | Jumped to when the program ends, with the exit status in B (0 for a normal end). Must not return. |
| `PLM_HALT` | Jumped to by `HALT`. |
| each `EXTERNAL` | A label with the procedure's PL/M name in upper case. |

The startup code saves the stack pointer it was entered with in `RT_ENTRYS`.
A hooks file that sets `RT_STACKTOP` to 0 can therefore return to whatever
started the program (BASIC, a monitor or an operating system) with
`LDS RT_ENTRYS` / `RTS`. The CoCo hooks do this.

## Calling convention for EXTERNAL procedures

* **Arguments:** each one is pushed as a **16-bit word**, from left to right.
  BYTE values are zero-extended first.
* **Where to find them:** on entry, the last argument is at `2,S`, just above
  the return address, the one before it at `4,S`, and so on. The last
  argument is **also in D**, so most one-argument hooks never need to touch
  the stack.
* **Cleanup:** the caller removes the arguments after the call
  (`LEAS 2n,S`).
* **Results:** a BYTE result is returned in **B** and an ADDRESS result in
  **D**.
* **Registers:** a hook may change A, B, X, Y, U, DP and CC. Only S must be
  preserved. Keep DP at 0 if the hook calls code that relies on it.
* **Runtime routines:** hooks are assembled after `plm_rt.asm`, so they may
  call `RT_MUL16` and `RT_DIV16`.

Example: a two-argument hook `POKE(ADDR, VALUE)`:

```
; POKE: PROCEDURE (ADDR, VALUE) EXTERNAL; DECLARE ADDR ADDRESS, VALUE BYTE;
POKE    LDX     4,S         ; first argument: ADDR
        STB     ,X          ; last argument: VALUE, also in D (B = low byte)
        RTS
```

## Hooks provided

All three hooks files implement the same procedures, so a program that uses
only these runs unchanged on any of the targets:

```
PUTCH:  PROCEDURE (C) EXTERNAL;    DECLARE C BYTE;    END PUTCH;
PUTDEC: PROCEDURE (N) EXTERNAL;    DECLARE N ADDRESS; END PUTDEC;
GETCH:  PROCEDURE BYTE EXTERNAL;                      END GETCH;
EXIT:   PROCEDURE (CODE) EXTERNAL; DECLARE CODE BYTE; END EXIT;
```

`PUTDEC` prints a number in decimal followed by a newline.

| | `runtime/sim` | `runtime/coco` | `runtime/example` |
|---|---|---|---|
| `PUTCH` | simulator I/O port | Color BASIC CHROUT; LF sent as CR | MC6850 ACIA, waits until ready to send |
| `GETCH` | stdin; 0 at end of input | waits for a key (POLCAT); ENTER gives 13 | ACIA, waits for a character |
| `PUTDEC` | simulator decimal port | via `RT_DIV16` and CHROUT | via `RT_DIV16`, ends with CR LF |
| `EXIT`, end of program | simulator exit status | back to BASIC (status discarded) | stops in a loop |
| `HALT` | exit status 0 | back to BASIC | stops in a loop |
| extra | | `CLS` clears the screen | |
| stack | `$FF00` | BASIC's | `$8000` |

## Writing hooks for a new machine

Copy `runtime/example/plm_hooks.asm` to a directory of your own, change the
I/O routines and `RT_STACKTOP`, and assemble with `-I runtime -I yourdir`.
Add any extra routines your programs need as further labels, and declare them
in PL/M as `EXTERNAL`. The example file's `ACIA_BASE` and `RT_STACKTOP` are
wrapped in `IFNDEF`, so they can also be overridden from the lwasm command
line with `-D`.

# Running on a Color Computer

```
build/plmc --org 3000H -o hello.asm hello.plm
lwasm -9 -f decb -I runtime -I runtime/coco -o HELLO.BIN hello.asm
```

Copy `HELLO.BIN` to a disk or disk image (for example with toolshed:
`decb copy -2 -b HELLO.BIN disk.dsk,HELLO.BIN`), then in BASIC:

```
CLEAR 200,&H2FFF
LOADM"HELLO":EXEC
```

* **Load address:** always give `--org`. The default `$0100` would overwrite
  BASIC's RAM interrupt vectors. `CLEAR 200,&H2FFF` stops BASIC from using
  memory from `$3000` up.
* **Reset vector:** never use `--vectors` on a CoCo; `$FFFE` is in ROM.
* **Running and returning:** the program runs with DP = 0 on BASIC's own
  stack, and returns to BASIC when it ends, on `HALT`, or on `EXIT`.
* **Newlines:** Color BASIC ignores the LF character, so `PUTCH(10)` is sent
  as CR, the CoCo's newline. A program written for the simulator therefore
  prints the same way on the CoCo.
* **Lower case:** on a CoCo 1 or 2, lower-case letters appear as inverse
  video.

The test suite checks these hooks in two ways. They run in the simulator
against a stand-in for the BASIC ROM. They also run on an emulated CoCo 2
under XRoar: each program is loaded from a disk image with `LOADM` and
`EXEC`, and the text screen is compared with what's expected.

# Generated code

## Layout

```
        PRAGMA  6809
        ORG     $0100                 ; --org
        INCLUDE "plm_crt0.asm"        ; RT_START: set S and DP, call PLM_MAIN
PLM_MAIN                              ; main program statements
        ...
        RTS
P_PROC1                               ; each procedure
        ...
        INCLUDE "plm_rt.asm"          ; RT_MUL16, RT_DIV16
        INCLUDE "plm_hooks.asm"       ; target-specific hooks
V_X     RMB     1                     ; variables, parameters, locals
        END     RT_START
```

* **Startup:** execution starts at `RT_START`, at the `--org` address. It sets
  the stack (unless `RT_STACKTOP` is 0) and DP = 0, calls `PLM_MAIN`, and
  jumps to `PLM_EXIT` with status 0.
* **Variables** come last and are reserved with `RMB`, so they add nothing to
  S-record or `.BIN` files.
* **Total size:** for a typical small program, the code is a few hundred bytes
  and the runtime about 70 bytes.

## Example

The program from [Building and quick start] compiles to (in part):

```
; 6: DO WHILE I <= 10;
L1
        LDB     V_I
        CMPB    #$0A
        LBHI    L2
; 7: TOTAL = TOTAL + I;
        LDB     V_I
        CLRA
        PSHS    D
        LDD     V_TOTAL
        ADDD    ,S++
        STD     V_TOTAL
; 8: I = I + 1;
        LDB     V_I
        ADDB    #$01
        STB     V_I
        LBRA    L1
L2
```

Each statement is preceded by its source line as a comment, which makes the
listing and simulator traces easy to follow.

## Conventions

* **Registers:** a BYTE value is computed in B and an ADDRESS value in D. A
  BYTE is widened with `CLRA` only where a 16-bit value is needed.
* **Binary operators:** the right operand is evaluated first and pushed, then
  the left one, then they're combined with `op ,S+` or `op ,S++`. When the
  right operand is a constant or a variable of the right size, it's used
  directly as the instruction operand (`ADDB V_I`, `CMPB #$0A`).
* **Comparisons:** these use the unsigned branches `BLO`, `BHS`, `BHI`, `BLS`,
  `BEQ` and `BNE`.
  * In an `IF` or `WHILE`, the code branches straight on the comparison.
  * As a value, a comparison produces `$FF` or `$00` in B.
  * Statement-level control flow uses long branches, so there's no limit on
    the size of a block.
* **Multiplication and division:** BYTE × BYTE uses the 6809's `MUL`. All
  other multiplications and all divisions call `RT_MUL16` or `RT_DIV16`
  (left operand in X, right in D).
* **Procedure calls:** arguments are stored in the procedure's fixed
  parameter locations and the procedure is called with `JSR`. If an argument
  itself contains a call, all arguments are evaluated and pushed first, then
  stored. That makes `F(1, F(2, 3))` work even though `F` has only one set
  of parameter locations.
* **Label names:** PL/M names can't contain `_`, so these labels never clash
  with yours:

  | Label | Meaning |
  |---|---|
  | `V_X` | variable `X` of the program |
  | `Vn_X` | variable or parameter `X` of nested scope *n* |
  | `P_X`, `Pn_X` | procedure `X` |
  | `Lnnn` | compiler-generated label |
  | `RT_*` | runtime routine or data |
  | `PLM_*` | entry point the hooks provide |
  | `X` | `EXTERNAL` procedure `X` |

# Inspecting the compiler's view

`--tokens` prints each token with its line and column. Numbers are shown with
their value, which is useful for checking how a constant was read:

```
2:1 IDENT X
2:3 PUNCT =
2:5 NUMBER 255
2:10 PUNCT +
```

`--ast` prints the syntax tree after parsing, with expressions written as
prefix lists. It shows exactly how precedence was applied:

```
PROGRAM SUM
  PROCEDURE PUTDEC (N) EXTERNAL
    DECLARE N ADDRESS
  DECLARE I BYTE
  DECLARE TOTAL ADDRESS
  ASSIGN TOTAL = 0
  ASSIGN I = 1
  DO WHILE (<= I 10)
    ASSIGN TOTAL = (+ TOTAL I)
    ASSIGN I = (+ I 1)
  CALL (CALL PUTDEC TOTAL)
```

To follow a program at run time, assemble with a listing
(`lwasm ... --list=prog.lst`) and run `sim6809 -t`. The trace addresses
match the listing, and the listing shows the PL/M source line for each
group of instructions.

# Testing

`make test` runs all of these suites:

| Suite | What it checks |
|---|---|
| `lex`, `parse` | `--tokens` and `--ast` output for every number form, every keyword, precedence, dangling `ELSE` and nested procedures, against golden files |
| `err` | 44 programs that must fail, each with the expected message and position |
| `run` | complete programs compiled, assembled and run in `sim6809`; output and exit status compared. They cover every operator and conversion rule, control flow, input, procedures (including nested calls), `HALT`, `--org` and `--vectors` |
| `fuzz` | random programs with thousands of random expressions, checked against an independent C model of the typing rules; `make fuzz-test N=2000` runs more |
| `coco`, `xroar` | the Color Computer hooks, in the simulator and on an emulated CoCo 2 (the `xroar` suite is skipped if XRoar, the ROMs or toolshed aren't installed) |
| `sim` | the simulator itself (see the sim6809 manual) |

# Source code

| File | Contents |
|---|---|
| `src/main.c` | command line, file handling |
| `src/lexer.c` | tokens, numbers, strings, comments |
| `src/parser.c`, `ast.c` | recursive-descent parser, syntax tree, `--ast` |
| `src/symtab.c`, `sema.c` | scopes, name resolution, types, call checks, recursion detection |
| `src/codegen.c` | assembly output |
| `runtime/plm_crt0.asm`, `plm_rt.asm` | startup code, 16-bit multiply and divide |
| `runtime/*/plm_hooks.asm` | hooks for the simulator, the CoCo, and the ACIA template |
