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
make install    # copy plmc, sim6809 and srec2flex to BINDIR (default /usr/local/bin)
                # and their man pages to MANDIR/man1 (default /usr/local/share/man)
```

`make install BINDIR=~/bin` installs elsewhere; `PREFIX` (BINDIR is
`$(PREFIX)/bin`, MANDIR `$(PREFIX)/share/man`, DATADIR `$(PREFIX)/share`) and
`DESTDIR` (for staged installs) work as usual. `--version` prints each
program's version (0.1.0), and `man plmc`, `man sim6809` and `man srec2flex`
give a summary of each program's options.

<!-- installed-paths
The runtime and hooks files are installed in `@RUNTIME@`, and the commands
in this document use that directory.
-->
<!-- source-paths -->
The runtime and hooks files are installed in `DATADIR/plm09/runtime`, so an
installed plmc's output is assembled with `-I
/usr/local/share/plm09/runtime -I /usr/local/share/plm09/runtime/sim` in
place of `-I runtime -I runtime/sim`.
<!-- /source-paths -->

**Fedora RPM:** `make rpm` builds `plm09.spec` from the committed tree (HEAD;
uncommitted changes are left out) into `build/rpm`, running the test suites
that need only lwasm, python3 and groff; `make srpm` makes just the source
RPM (for `mock`). The RPM build applies a patch, made by
`tools/rpmdocs.py`, that changes the runtime paths in the documentation
(`-I runtime` and so on) to the installed directory, `%{_datadir}/plm09/runtime`,
and rebuilds the PDFs. Then:

```
sudo dnf install build/rpm/RPMS/x86_64/plm09-0.1.0-1.*.x86_64.rpm
```

It installs the programs in `/usr/bin`, the man pages, and the runtime in
`/usr/share/plm09/runtime`:

```
plmc -o hello.asm hello.plm
lwasm -9 -f srec -I /usr/share/plm09/runtime -I /usr/share/plm09/runtime/sim -o hello.s19 hello.asm
sim6809 hello.s19
```

plm09 is under the MIT license (see `LICENSE`).

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
| `--memory N` | with `--os9`: bytes of the data area for `MEMORY` (default 0) |
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
- **Built-ins:** all of PL/M-80's, as built-ins only where the name isn't declared:
  - `HIGH(W)`, `LOW(W)` and `DOUBLE(B)` convert between BYTE and ADDRESS; `SHL(P, N)`/`SHR(P, N)` shift a BYTE or ADDRESS, and `ROL(P, N)`/`ROR(P, N)` rotate a BYTE. They compile to inline code, so they work on every target.
  - `CARRY`, `ZERO`, `SIGN` and `PARITY` read the flags of the last `+ - PLUS MINUS SCL SCR DEC` executed; `SCL`/`SCR` rotate through the carry, and `DEC(A + B)` adjusts a BCD addition. Unlike on the 8080, these flags are exact: plmc keeps them in memory.
  - `INPUT(n)` and `OUTPUT(n) = v` read and write port n, the byte at `RT_PORTS + n` (the hooks set it: `$FF00` for the simulator's I/O block and the CoCo's I/O page).
  - `CALL MOVE(count, from, to)` copies bytes; `CALL TIME(n)` waits n × 100 µs, timed by the hooks' `RT_CYCLES100`.
  - `MEMORY` is the memory after the program (`--memory N` bytes under OS-9), and `STACKPTR` is the S register.
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
  - `PLUS` and `MINUS` add and subtract with the carry (borrow) of the last `+`, `-`, `PLUS`, `MINUS`, `SCL`, `SCR` or `DEC` executed, for numbers wider than 16 bits: `SL = AL + BL; SH = AH PLUS BH;`. Unlike the 8080 compiler's flag, this carry is well defined (nothing else changes it).
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
- embedded assignment `:=`

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
- **`runtime/flex/`**, for FLEX 9 commands. See [FLEX](#flex).
- **`runtime/simon6809/`**, for the Simon6809 board under its BUGGY monitor. See [Simon6809](#simon6809).
- **`runtime/kswichit/`**, for Wichit Sirichote's 6809 Microprocessor Kit under its monitor. See [6809 Microprocessor Kit](#6809-microprocessor-kit).
- **`runtime/searle6809/`**, for Grant Searle's 6809 SBC (and Jeff Tranter's PCB of it), under the ASSIST09 monitor or BASIC. See [Grant Searle's 6809 SBC](#grant-searles-6809-sbc).
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

## FLEX

```
build/plmc -o sum.asm sum.plm
lwasm -9 -f srec -I runtime -I runtime/flex -o sum.s19 sum.asm
build/srec2flex sum.s19 SUM.CMD
```

* **Running it:** copy `SUM.CMD` to a FLEX disk and type `SUM`. `build/sim6809 --flex SUM.CMD` runs it in the simulator, which emulates FLEX's DOS entry points.
* **Any FLEX 9 system:** the hooks only use FLEX's jump table at `$CD00` (PUTCHR, PCRLF, GETCHR, OUTDEC, WARMS). The program returns to FLEX when it ends.
* **Machine-dependent values:** the stack top (`RT_STACKTOP`, default `$C700`, the top of FLEX's utility command area), the I/O ports (`RT_PORTS`, `$E000`) and the clock for `TIME` (`RT_CYCLES100`, 1 MHz) can be set with lwasm's `-D`.
* **`srec2flex`** converts lwasm's S-records to a FLEX binary (`.CMD`), using the S9 entry address as the transfer address.
* **Tested on real FLEX:** the `dragonflex` suite runs the test programs on Compusense FLEX for the Dragon 64 under XRoar (`$FLEX_DISK`, default `~/coco/flex/FLEX_1.3.2.VDK`).

## Simon6809

```
build/plmc --org 0400H -o sum.asm sum.plm
lwasm -9 -f srec -I runtime -I runtime/simon6809 -o sum.s19 sum.asm
```

* **Running it:** send the S1/S9 lines of `sum.s19` at the BUGGY monitor's prompt and type `G400`. The program returns to the monitor (with `SWI`), which shows the registers, B being the exit status.
* **The board** ([drpeker/simon6809-ROM-Monitor](https://github.com/drpeker/simon6809-ROM-Monitor)): a 1 MHz 6809, 32 KB RAM (programs from `$0400`), an FT245RL USB FIFO console that the hooks use through the monitor's `getchar`/`putchar` vectors, and RAM interrupt vectors that `PLM_SETINT` patches.
* **Tested** on the board's real monitor ROM in `sim6809` (`--ft245`, `--swi`): the `simon6809` suite (`$SIMON_ROM`, default `~/coco/simon6809/simon09.rom.s19`).

## 6809 Microprocessor Kit

```
build/plmc --org 0200H -o sum.asm sum.plm
lwasm -9 -f srec -I runtime -I runtime/kswichit -o sum.s19 sum.asm
```

* **Running it:** press LOAD, send `sum.s19` over the serial port (19200 8N1, 1 ms character delay), then GO (from `$0200`). The program returns to the monitor with `SWI`.
* **The kit** (Wichit Sirichote, [kswichit.net/6809/6809.htm](https://kswichit.net/6809/6809.htm)): a 68B09 at 1.2288 MHz, 32 KB RAM, an MC6850 ACIA at `$A000` that the hooks use directly (the monitor's routine addresses vary between versions), I/O ports at `$8000` (`OUTPUT(0)` drives the GPIO1 LEDs), and RAM interrupt vectors at `$7FF0`--`$7FF8` that `PLM_SETINT` patches. The stack is at `$7000`, below the monitor's variables.
* **Tested** in `sim6809` with an ACIA (`--acia`) and a stand-in monitor: the `kswichit` suite.

## Grant Searle's 6809 SBC

```
build/plmc --org 1000H -o sum.asm sum.plm
lwasm -9 -f srec -I runtime -I runtime/searle6809 -o sum.s19 sum.asm
```

* **Running it** with Jeff Tranter's combined ROM: type `L` at ASSIST09's `>` prompt, send `sum.s19`'s S1/S9 lines, then `C 1000` (call; the program returns with RTS, and ASSIST09 shows the registers, B being the exit status). With Grant Searle's BASIC ROM: get the program into RAM, then `CLEAR 200,&HFFF` and `EXEC &H1000`.
* **The board** ([searle.wales/6809/Simple6809.html](http://searle.wales/6809/Simple6809.html)): a 68B09 at 1.8432 MHz, 32 KB RAM, a 6850 ACIA at `$A000` that the hooks use directly, and a 16 KB ROM. `PLM_SETINT` handles both ROMs' interrupt routing (ASSIST09's RAM vector table, or BASIC's RAM JMPs).
* **Tested** on both real ROMs in `sim6809` (`--acia`, `--rom`, `--reset`, Intel HEX loading): the `searle6809` suite (`$SEARLE_ROM`, `$SEARLE_BASIC`, defaults in `~/coco/searle6809/`).

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
build/sim6809 --flex PROG.CMD
```

| Option | Meaning |
|---|---|
| `-t` | trace each instruction to stderr |
| `-n N` | stop after N instructions (default 100M) |
| `--io BASE` | move the I/O block from `$FF00` |
| `--entry ADDR` | start address, if the file has no S9 record |
| `--zero` | clear memory instead of filling it with `$A5` |
| `--swi` | execute `SWI`, `SWI2` and `SWI3` through their vectors instead of stopping |
| `--acia ADDR` | an MC6850 ACIA at `ADDR`/`ADDR+1` on stdin/stdout (reads 0 at end of input) |
| `--ft245 ADDR` | an FT245RL USB FIFO at `ADDR` on stdin/stdout, asserting FIRQ while input waits |
| `--stop-at-end` | with `--acia`: waiting for input after its end ends the run (status 0) |
| `--reset` | start at the reset vector (`$FFFE`), for ROM images |
| `--rom ADDR` | `ADDR`-`$FFFF` is ROM: writes are ignored |

S-records and Intel HEX files (as many ROMs come) are both loaded.

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
| `man` | the man pages format without groff warnings and name every `--help` option |
| `lex`, `parse` | `--tokens` and `--ast` output against golden files |
| `err` | each program must fail to compile with the expected message |
| `run` | compile, assemble, run, and compare output and exit status |
| `coco` | the CoCo hooks in `sim6809` against a stand-in ROM |
| `os9` | the run programs and fuzz programs as OS-9 modules in `sim6809 --os9`, at two load addresses |
| `nitros9` | the run programs on NitrOS-9 Level 2 booted under XRoar (skipped without XRoar, toolshed, `$NITROS9_DISK` and `$COCO3_ROM`) |
| `xroar` | the CoCo programs on an emulated CoCo 2, comparing the screen (skipped without XRoar, ROMs and toolshed) |
| `fuzz` | random expression programs checked against a C model of the typing rules; `make fuzz-test N=2000` runs more |
