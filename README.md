# plm09: a minimal PL/M compiler for the 6809

`plmc` compiles a small subset of PL/M-80 into 6809 assembly source for
[lwasm](http://www.lwtools.ca/) (lwtools 4.24). The output is not tied to any
machine: all I/O goes through a small hooks file that you supply. The repository
also contains `sim6809`, a headless 6809 simulator used to run the test suite.

Full manuals: [docs/plmc.pdf](docs/plmc.pdf) and
[docs/sim6809.pdf](docs/sim6809.pdf) (Markdown sources next to them).

## Building

Requires gcc, make and lwtools. `make test` also needs python3, and its
`xroar` suite uses XRoar with CoCo ROMs and toolshed's `decb` when they are
installed.

```
make            # build/plmc and build/sim6809
make test       # all test suites
```

## Quick start

```
/* hello.plm */
HELLO: DO;
PUTCH: PROCEDURE (C) EXTERNAL; DECLARE C BYTE; END PUTCH;
PUTDEC: PROCEDURE (N) EXTERNAL; DECLARE N ADDRESS; END PUTDEC;
DECLARE I BYTE;
I = 1;
DO WHILE I <= 5;
    CALL PUTDEC(I * I);
    I = I + 1;
END;
CALL PUTCH('!');
END HELLO;
```

```
build/plmc -o hello.asm hello.plm
lwasm -9 -f srec -I runtime -I runtime/sim -o hello.s19 hello.asm
build/sim6809 hello.s19
```

`make run F=hello.plm` does all three steps.

For real hardware, replace `-I runtime/sim` with the directory that holds your
own `plm_hooks.asm` (see [Hooks](#hooks)). You can also choose another output
format, such as `-f decb` or `-f raw`.

### plmc options

| Option | Meaning |
|---|---|
| `-o FILE` | output file (default: stdout) |
| `--org ADDR` | load and start address, default `$0100` (`$2000`, `2000H` and `0x2000` are all accepted) |
| `--vectors` | also emit `FDB RT_START` at `$FFFE`, the reset vector, and the vectors of INTERRUPT procedures |
| `--os9` | generate a position-independent OS-9 program module |
| `--stack N` | with `--os9`: stack bytes in the data area (default 256) |
| `--name NAME` | with `--os9`: module name (default: the program's name) |
| `--tokens` | print the tokens and stop |
| `--ast` | print the syntax tree and stop |

## Language

A program is a labeled block:

```
NAME: DO;
    declarations
    statements
END NAME;
```

The source is case-insensitive. `$` is ignored inside names and numbers
(`1$000`), and comments are written `/* */`.

**Supported**
- **Types:** `BYTE` (8-bit unsigned) and `ADDRESS` (16-bit unsigned) scalars and arrays.
  - `DECLARE X BYTE;`
  - `DECLARE (A, B) ADDRESS;`
  - `DECLARE X BYTE, Y ADDRESS;`
- **Constants:**
  - decimal numbers, or with a base suffix: `0FFH` (hex), `1010B` (binary), `17O`/`17Q` (octal), `12D` (decimal)
  - one- and two-character strings: `'A'` is a BYTE, `'AB'` is an ADDRESS
- **Operators**, from highest to lowest precedence:
  1. unary `-`
  2. `* / MOD`
  3. `+ -`
  4. `= <> < > <= >=`
  5. `NOT`
  6. `AND`
  7. `OR XOR`
- **Macros:** `DECLARE TRUE LITERALLY '0FFH';` and the like.
  - The text, up to 255 characters, is substituted for the name before parsing, so `DCL LITERALLY 'DECLARE'` works too.
  - Literals follow block scope, and inner declarations can hide them.
  - `--ast` shows the result of the substitution.
- **Initialized data:**
  - `DECLARE GREETING (*) BYTE INITIAL ('HELLO', 0);` and `DECLARE DAYS (*) BYTE DATA (31, 28, ...);`.
  - Values fill scalars in storage order. Strings of any length up to 255 characters supply one character per BYTE and two per ADDRESS.
  - Initial values can also be location references (`.BUF + 2`), and `(*)` sizes an array from its list.
  - INITIAL is for the main program's outer level. DATA is stored with the code, can be used anywhere, and can't be assigned.
- **Structures:**
  - `DECLARE AIRPLANE (20) STRUCTURE (SPEED BYTE, ALTITUDE ADDRESS);`, with members referenced as `AIRPLANE(I).SPEED`; members may be arrays (`X(i).M(j)`).
  - Structures can be factored and BASED. Whole structures are used only with `.` and `LENGTH`/`LAST`/`SIZE`.
- **Pointers:**
  - `DECLARE ITEM BASED PTR BYTE;` makes `ITEM` mean whatever `PTR` points at; BASED arrays work too.
  - `.X`, `.A(i)` and `.P` give the address of a variable, an element or an outer-level procedure.
  - `.('TEXT', 0)` gives the address of constant bytes stored with the program.
  - `DECLARE (LO, HI) BYTE AT (.WORD);` makes aliases that share another variable's storage, and `DECLARE PORT BYTE AT (0FF00H);` puts a variable at a fixed address (for I/O registers). The 6809 is big-endian, so a BYTE AT an ADDRESS is its high-order byte.
- **Arrays:**
  - `DECLARE BUF(80) BYTE;` declares `BUF(0)` to `BUF(79)`.
  - Elements can be used anywhere a variable can, including as assignment targets.
  - Subscripts are any expression and aren't range-checked, as in PL/M-80.
  - A plain `BUF` means `BUF(0)`.
  - `LENGTH(BUF)` and `LAST(BUF)` give the number of elements and the highest subscript (BYTE if 255 or less, as in PL/M-80), and `SIZE(X)` the number of bytes.
  - Arrays can't be parameters.
- **Statements:**
  - assignment, including several targets (`A, B = 0;`)
  - `IF ... THEN ... [ELSE ...]`
  - `DO; ... END;` blocks, which may have their own declarations
  - `DO WHILE ...; ... END;`
  - `DO I = start TO limit [BY step]; ... END;`, with PL/M-80's rules: the limit and step are re-evaluated on each pass, and the loop ends when the index wraps around, so `DO I = 0 TO 255` with a BYTE `I` ends
  - `DO CASE expr; case-0; case-1; ... END;`; an out-of-range selector runs no case
  - `CALL`, `RETURN [expr]`, `HALT`, and the empty statement `;`
- **Procedures:**
  - `P: PROCEDURE (A, B) [BYTE|ADDRESS] [EXTERNAL|REENTRANT] [INTERRUPT n]; ... END P;`
  - `INTERRUPT 0`, `1` and `2` are IRQ, FIRQ and NMI. The procedure runs when that interrupt occurs and returns with `RTI`; it must acknowledge the device itself. The hooks' `PLM_SETINT` installs it at start-up (CoCo: BASIC's RAM vectors, restored at exit), and `--vectors` puts it in the vector table. `ENABLE;` and `DISABLE;` unmask and mask IRQ and FIRQ. Not available with `--os9`.
  - Each parameter must be declared in the procedure body.
  - Procedures may be nested, and a nested procedure can see the variables of the procedures around it.
  - A typed procedure with no parameters is called just by naming it in an expression.
- **Labels and GOTO:**
  - Any statement may be labeled (`L1: L2: stmt`), and `GOTO L` / `GO TO L` jumps to a label in the same block or an enclosing one (never into a block).
  - A GOTO out of a procedure may only go to the outer level of the main program; the stack is reset.
  - `END name` after a DO is checked against its labels.
  - `DECLARE (L1, L2) LABEL;` declares labels at the start of a block; they must label statements at the outer level of that block. `LABEL PUBLIC` also exports the label to assembly code under its own name, and `LABEL EXTERNAL` names an assembly-language label (in the hooks, say) that GOTO can jump to.

**Semantics** (following PL/M-80)
- **Unsigned:** all arithmetic and comparisons are unsigned.
- **Result types:**
  - `+ - MOD AND OR XOR` give BYTE when both operands are BYTE, and ADDRESS otherwise. BYTE results wrap at 8 bits: with BYTE `B = 200`, `B + 100` is 44, even when it is assigned to an ADDRESS.
  - `*` and `/` always give an ADDRESS.
  - Relational operators give a BYTE: `0FFH` for true and `0` for false.
  - Unary `-` and `NOT` keep the type of their operand.
  - A constant is a BYTE if it is 255 or less.
- **Conditions:** `IF` and `WHILE` test only bit 0 of the condition, so `IF 2` is false and `IF 3` is true. `AND` and `OR` always evaluate both sides.
- **Conversions:** assigning an ADDRESS to a BYTE keeps the low byte, and a BYTE is zero-extended when it becomes an ADDRESS. Arguments and `RETURN` values are converted to the declared parameter or result type in the same way.
- **Division by zero** gives a quotient of `0FFFFH` and a remainder equal to the dividend. Programs should not rely on this.
- **Declaration order:** a name must be declared before it is used.
- **Recursion needs REENTRANT:** as in classic PL/M, an ordinary procedure's parameters and locals are stored in fixed RAM locations, and the compiler reports direct and indirect recursion through such a procedure as an error. A `REENTRANT` procedure keeps them on the stack (Y-relative frame) and may be recursive; inside a REENTRANT procedure, REENTRANT procedures may be called before they are declared.
- **Undefined values:** variables start with whatever is in memory. The simulator fills memory with `$A5` so that use of an uninitialized variable shows up.

**Not supported yet:**
- `PUBLIC`
- `PLUS` and `MINUS` (add and subtract with carry)
- embedded assignment `:=`
- built-in procedures other than `LENGTH`, `LAST` and `SIZE`, such as `HIGH`, `LOW` and `SHL`

The compiler reports these with an error ("... is not supported") rather than
misreading them.

## Hooks

The generated file `INCLUDE`s `plm_crt0.asm`, `plm_rt.asm` (both in `runtime/`)
and `plm_hooks.asm`. lwasm finds `plm_hooks.asm` through `-I`, so the same
compiler output can target any machine.

**Symbols a hooks file must define**

| Symbol | Purpose |
|---|---|
| `RT_STACKTOP` | the initial S. The stack grows down from here. 0 means keep the caller's stack; the caller's S is saved in `RT_ENTRYS`, so `PLM_EXIT` can return with `LDS RT_ENTRYS` / `RTS`. |
| `PLM_EXIT` | jumped to when the program finishes, with the exit status in B (0 for a normal end) |
| `PLM_HALT` | jumped to by `HALT` |
| one label per `EXTERNAL` procedure | the procedure's PL/M name, in upper case |

**Calling convention for EXTERNAL procedures**
- **Arguments:** each one is pushed as a 16-bit word, left to right (BYTE values are zero-extended first). On entry, the last argument is at `2,S`, just above the return address, and is also in D.
- **Cleanup:** the caller removes the arguments.
- **Results:** a BYTE result is returned in B and an ADDRESS result in D.
- **Registers:** a hook may change any register except S.

A one-argument hook therefore needs no stack access:

```
PUTCH   STB   $FF00     ; PUTCH: PROCEDURE (C) EXTERNAL; DECLARE C BYTE;
        RTS
```

**Provided hooks files**
- **`runtime/sim/`**, for `sim6809`, which has its I/O at `$FF00`. It provides `PUTCH`, `PUTDEC` (prints a number and a newline), `GETCH` (returns 0 at end of input) and `EXIT`.
- **`runtime/coco/`**, for the Tandy Color Computer 1/2/3 under Color BASIC. It provides `PUTCH`, `PUTDEC`, `GETCH`, `EXIT` and `CLS`. See [Color Computer](#color-computer).
- **`runtime/os9/`**, for OS-9 and NitrOS-9 program modules built with `--os9`. See [OS-9](#os-9).
- **`runtime/example/`**, a template for an MC6850 ACIA board. It has the same four procedures, with `PUTDEC` written on top of `RT_DIV16`. Copy it and adjust the addresses for your machine. The test suite only checks that it assembles.

## Color Computer

The `runtime/coco/` hooks call the Color BASIC ROM, so programs run under
Color BASIC, Extended BASIC or Disk BASIC on a CoCo 1, 2 or 3:

```
build/plmc --org 3000H -o hello.asm hello.plm
lwasm -9 -f decb -I runtime -I runtime/coco -o HELLO.BIN hello.asm
```

Then, on the CoCo:

```
CLEAR 200,&H2FFF
LOADM"HELLO":EXEC
```

- **Load address:** use `--org`. The default `$0100` would overwrite BASIC's RAM interrupt vectors at `$0100`–`$0111`. `CLEAR 200,&H2FFF` keeps BASIC out of memory from `$3000` up. Never use `--vectors` here, because `$FFFE` is in ROM.
- **Stack and exit:** the program runs on BASIC's stack. It returns to BASIC when it ends, on `HALT`, or when it calls `EXIT`, and BASIC discards the exit status.
- **Output:** `PUTCH` and `PUTDEC` print through the ROM's CHROUT routine. Color BASIC ignores LF, so `PUTCH(10)` is sent as CR, the CoCo's newline. That way programs written for the simulator print correctly. Lower-case letters show as inverse video on a CoCo 1/2.
- **Input:** `GETCH` waits for a key through the ROM's POLCAT routine. ENTER gives 13.
- **Clearing the screen:** `CLS` (`CLS: PROCEDURE EXTERNAL; END CLS;`) calls the ROM's clear-screen routine.

**Testing**
- `make coco-test` runs the CoCo hooks in `sim6809` with a stand-in for the ROM (`tests/coco/fakerom.asm`). The stand-in also checks that the program returns to BASIC with the stack intact.
- `make xroar-test` runs the same programs on an emulated CoCo 2 with Disk Extended Color BASIC 1.1. It writes each program to a disk image with toolshed's `decb`, has XRoar type `LOADM"T":EXEC`, and compares the text screen with `tests/coco/*.screen`. It is skipped when XRoar, `decb` or the ROMs are missing.

## OS-9

```
build/plmc --os9 --name sum -o sum.asm sum.plm
lwasm -9 -f os9 -I runtime -I runtime/os9 -o sum sum.asm
```

* **Running it:** copy `sum` to `CMDS` on an OS-9 disk (with toolshed:
  `os9 copy sum disk.dsk,CMDS/sum` and `os9 attr -e -pe disk.dsk,CMDS/sum`)
  and run it from the shell. `build/sim6809 --os9 sum` runs it without OS-9.
* **What `--os9` produces:**
  * The module is position-independent (`LBSR`/`LBRA` instead of
    `JSR`/`JMP`) and reentrant.
  * All variables live in the process's data area and are addressed through
    U.
  * The hooks use `I$Write`, `I$Read` and `F$Exit`.
* **Choosing the module name:** give it the name of the file it will be
  installed as. If it matches a command already in memory (for example
  `echo`), OS-9 runs that command instead.

## Generated code

- **Memory layout:** the code starts at `--org`. It begins with `RT_START`, which sets up the stack and DP=0, then calls the main program (`PLM_MAIN`). The procedures, the runtime and the hooks follow, and the variables come last (`RMB`, so they add nothing to the load file).
- **Registers:** BYTE values are computed in B and ADDRESS values in D.
- **Multiply and divide:** BYTE×BYTE uses the `MUL` instruction directly. Other multiplications and all divisions call `RT_MUL16` and `RT_DIV16` in `runtime/plm_rt.asm`.
- **Source comments:** each statement is preceded by its source line as a comment.
- **Label names:** PL/M names can't contain `_`, so these prefixes never clash with your names.

  | Label | Meaning |
  |---|---|
  | `V_X` | global variable |
  | `Vn_X` | variable in nested scope n |
  | `M_X` / `Mn_X` | variable AT a fixed address |
  | `P_X` / `Pn_X` | procedure |
  | `G_X` / `Gn_X` | statement label |
  | `Lnnn` | compiler-generated label |
  | `RT_*` | runtime routine |
  | `PLM_*` | hook entry point |
  | exact name | EXTERNAL procedure |

## sim6809

```
build/sim6809 [-t] [-n N] [--io BASE] [--entry ADDR] [--zero] prog.s19
build/sim6809 --os9 [--load ADDR] [--data ADDR] [--param STR] module
```

| Option | Meaning |
|---|---|
| `-t` | trace each instruction to stderr |
| `-n N` | stop after N instructions (default 100M) |
| `--io BASE` | move the I/O block from `$FF00` |
| `--entry ADDR` | start address, if the file has no S9 record |
| `--zero` | clear memory instead of filling it with `$A5` |

**I/O block**

| Address | Access | Meaning |
|---|---|---|
| `$FF00` | write | put a character |
| `$FF01` | read | get a character (0 at end of input) |
| `$FF02` | write | exit with this status |
| `$FF04`/`$FF05` | write | print the 16-bit value and a newline, triggered by the write to `$FF05` (`STD $FF04`) |
| `$FF06` | write / read | assert interrupt lines (1 IRQ, 2 FIRQ, 4 NMI) / read the asserted lines |
| `$FF07` | write | release interrupt lines |
| `$FF08`/`$FF09` | write | timer: assert lines every N instructions, started by the write to `$FF09` (0 stops it) |
| `$FF0A` | write | the lines the timer asserts (default IRQ) |

**Exit status**
- the value the program writes to `$FF02`
- 124 if the instruction limit is reached
- 125 for an illegal opcode or an unsupported instruction (SWI, SWI2, SWI3)

It implements the full MC6809 instruction set, IRQ/FIRQ/NMI (level-triggered IRQ and FIRQ, as on the chip) with `CWAI` and `SYNC`, but not cycle timing.

## Tests

`make test` runs these suites (each also has its own target, such as `make run-test`):

| Suite | What it checks |
|---|---|
| `sim` | 6809 self-tests written in assembly with hand-checked expected values; the runtime routines; and every opcode in every addressing mode against lwasm's listing |
| `lex`, `parse` | `--tokens` and `--ast` output against golden files |
| `err` | each program must fail to compile with the expected message |
| `run` | compile, assemble, run, and compare output and exit status |
| `coco` | the CoCo hooks in `sim6809` against a stand-in ROM |
| `os9` | the run programs and fuzz programs as OS-9 modules in `sim6809 --os9`, at two load addresses |
| `nitros9` | the run programs on NitrOS-9 Level 2 booted under XRoar (skipped without XRoar, toolshed, `$NITROS9_DISK` and `$COCO3_ROM`) |
| `xroar` | the CoCo programs on an emulated CoCo 2, comparing the screen (skipped without XRoar, ROMs and toolshed) |
| `fuzz` | random expression programs checked against a C model of the typing rules; `make fuzz-test N=2000` runs more |
