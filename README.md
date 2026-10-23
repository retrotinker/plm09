# plm09: a minimal PL/M compiler for the 6809

`plmc` compiles a small subset of PL/M-80 into 6809 assembly source for
[lwasm](http://www.lwtools.ca/) (lwtools 4.24). The output is not tied to any
machine: all I/O goes through a small hooks file that you supply. The repository
also contains `sim6809`, a headless 6809 simulator used to run the test suite.

## Building

Requires gcc, make and lwtools. `make test` also needs python3.

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
| `--vectors` | also emit `FDB RT_START` at `$FFFE`, the reset vector |
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
- **Types:** `BYTE` (8-bit unsigned) and `ADDRESS` (16-bit unsigned) scalars.
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
- **Statements:**
  - assignment, including several targets (`A, B = 0;`)
  - `IF ... THEN ... [ELSE ...]`
  - `DO; ... END;` blocks, which may have their own declarations
  - `DO WHILE ...; ... END;`
  - `CALL`, `RETURN [expr]`, `HALT`, and the empty statement `;`
- **Procedures:**
  - `P: PROCEDURE (A, B) [BYTE|ADDRESS] [EXTERNAL]; ... END P;`
  - Each parameter must be declared in the procedure body.
  - Procedures may be nested, and a nested procedure can see the variables of the procedures around it.
  - A typed procedure with no parameters is called just by naming it in an expression.
- **Labels:** `DO` blocks may be labeled, and `END label` is checked against the label.

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
- **No recursion:** as in classic PL/M, parameters and locals are stored in fixed RAM locations, so procedures are not reentrant. The compiler reports direct and indirect recursion as errors.
- **Undefined values:** variables start with whatever is in memory. The simulator fills memory with `$A5` so that use of an uninitialized variable shows up.

**Not supported yet:**
- arrays, `BASED` variables, structures, `LITERALLY`
- `INITIAL` and `DATA`
- the address-of operator `.`
- iterative `DO I = ... TO ...` and `DO CASE`
- `GOTO` and labels on other statements
- `REENTRANT`, `INTERRUPT`, `PUBLIC`
- `PLUS` and `MINUS` (add and subtract with carry)
- embedded assignment `:=`
- built-in procedures such as `HIGH`, `LOW` and `SHL`

The compiler reports these with an error ("... is not supported") rather than
misreading them.

## Hooks

The generated file `INCLUDE`s `plm_crt0.asm`, `plm_rt.asm` (both in `runtime/`)
and `plm_hooks.asm`. lwasm finds `plm_hooks.asm` through `-I`, so the same
compiler output can target any machine.

**Symbols a hooks file must define**

| Symbol | Purpose |
|---|---|
| `RT_STACKTOP` | the initial S. The stack grows down from here. |
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
- **`runtime/example/`**, a template for an MC6850 ACIA board. It has the same four procedures, with `PUTDEC` written on top of `RT_DIV16`. Copy it and adjust the addresses for your machine. The test suite only checks that it assembles.

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
  | `P_X` / `Pn_X` | procedure |
  | `Lnnn` | compiler-generated label |
  | `RT_*` | runtime routine |
  | `PLM_*` | hook entry point |
  | exact name | EXTERNAL procedure |

## sim6809

```
build/sim6809 [-t] [-n N] [--io BASE] [--entry ADDR] [--zero] prog.s19
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

**Exit status**
- the value the program writes to `$FF02`
- 124 if the instruction limit is reached
- 125 for an illegal opcode or an unsupported instruction (SWI, SYNC, CWAI)

It implements the full MC6809 instruction set, but not interrupts or cycle timing.

## Tests

`make test` runs these suites (each also has its own target, such as `make run-test`):

| Suite | What it checks |
|---|---|
| `sim` | 6809 self-tests written in assembly with hand-checked expected values; the runtime routines; and every opcode in every addressing mode against lwasm's listing |
| `lex`, `parse` | `--tokens` and `--ast` output against golden files |
| `err` | each program must fail to compile with the expected message |
| `run` | compile, assemble, run, and compare output and exit status |
| `fuzz` | random expression programs checked against a C model of the typing rules; `make fuzz-test N=2000` runs more |
