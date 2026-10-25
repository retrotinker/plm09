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
include path. Four hooks files come with plmc:

| Hooks | Target |
|---|---|
| `runtime/sim/` | `sim6809`, the headless simulator used by the test suite |
| `runtime/coco/` | the Tandy Color Computer 1, 2 and 3 under Color BASIC |
| `runtime/os9/` | OS-9 and NitrOS-9 (Level 1 and 2), for modules built with `--os9` |
| `runtime/example/` | a template for a board with an MC6850 ACIA serial port |

**Main features**

* BYTE (8-bit) and ADDRESS (16-bit) unsigned variables, arrays and
  structures, with PL/M-80's rules for mixing the two types, and the
  built-ins `LENGTH`, `LAST` and `SIZE`.
* All PL/M arithmetic, logical and relational operators, with PL/M precedence.
* `LITERALLY` macros, scoped like other declarations.
* Pointers: `BASED` variables and the address-of (dot) operator `.X`.
* `IF`/`THEN`/`ELSE`, `DO` blocks with their own declarations, `DO WHILE`
  and iterative `DO I = ... TO ... BY ...` loops, `DO CASE`, labels and
  `GOTO`, `HALT`.
* Procedures: typed and untyped, with parameters, nested inside each other, and
  `EXTERNAL` procedures written in assembly.
* Error messages that give the file, line and column. Every unsupported PL/M
  feature is rejected with a clear message rather than misread.
* Readable output: each source statement appears as a comment above the code
  generated for it.
* With `--os9`, position-independent, reentrant OS-9 program modules that
  run under OS-9 and NitrOS-9.

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
| `--os9` | Generate a position-independent OS-9 program module instead of a program for a fixed address. See [OS-9 modules]. |
| `--stack N` | With `--os9`: bytes of stack to reserve in the module's data area (default 256, at least 32). |
| `--name NAME` | With `--os9`: the module name (default: the PL/M program's name). |
| `--tokens` | Print the token stream and stop. See [Inspecting the compiler's view]. |
| `--ast` | Print the syntax tree and stop. |
| `-h`, `--help` | Print a usage summary and exit. |

The `--org` address and `--stack` size can be written as decimal (`8192`),
`0x2000`, `$2000` (quote it for the shell) or PL/M style `2000H`, and must fit
in 16 bits. `--org` and `--vectors` can't be combined with `--os9`, and
`--stack` and `--name` need `--os9`; violations are reported as errors.

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
  Strings of up to 255 characters are allowed as the body of a `LITERALLY`
  declaration. Anywhere else, a string longer than two characters is an
  error.
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

## LITERALLY

```
DECLARE TRUE LITERALLY '0FFH', FALSE LITERALLY '0';
DECLARE BUFFER$SIZE LITERALLY '32';
DECLARE DCL LITERALLY 'DECLARE', PROC LITERALLY 'PROCEDURE';

DCL PRINT$BUFFER(BUFFER$SIZE) BYTE;
```

`DECLARE name LITERALLY 'text'` defines a parameterless macro. Every later
occurrence of *name* is replaced by the tokens of *text*, before parsing, so a
literal can stand for anything:

* a constant (`TRUE`, `BUFFER$SIZE`), including where only a number is
  allowed, such as an array dimension
* a keyword (`DCL`, `PROC`)
* part of an expression
* a whole statement (`INCR LITERALLY 'N = N + 1'`, then `INCR;`)

The rules:

* **Text:** up to 255 characters. Write a quote inside it as `''`, so
  `QUOTE LITERALLY '''A'''` stands for `'A'`.
* **Nesting:** a literal's text may use other literals, which are expanded in
  turn. A literal that expands to itself is reported as
  `literal X expands recursively`.
* **Scope:** literals follow block scope like any other declaration.
  * A literal declared in a procedure or a `DO` block is visible only there,
    from its declaration on.
  * An inner block can declare its own literal, or a variable or procedure,
    with the same name, which hides the outer literal until that block's
    `END`.
  * Declaring the same name twice in one block is an error.
* **Names being defined are not expanded:** declared names, procedure names,
  parameters, labels and the name after `END`.
* **Textual substitution:** the text is substituted *as text*, so precedence
  can surprise you. After `DECLARE TWO LITERALLY '1 + 1'`, the expression
  `TWO * 3` means `1 + 1 * 3`, which is 4. Put parentheses in the literal
  (`'(1 + 1)'`) when it is an expression.
* **Form:** a LITERALLY can't be factored (`DECLARE (A, B) LITERALLY ...`) or
  have a dimension, but it can be mixed with other items in one `DECLARE`.
* **Seeing the result:** `--ast` shows the program *after* substitution,
  which is the easiest way to check what a literal turned into.

## Arrays

```
DECLARE BUF(80) BYTE;
DECLARE (XS, YS)(10) ADDRESS;
DECLARE COUNT BYTE, TABLE(16) ADDRESS;
```

* **Declaring:** a dimension in parentheses makes an array. `BUF(80)` has the
  80 elements `BUF(0)` to `BUF(79)`. The dimension must be a number from 1
  to 65535, and the array must fit in 64 KB.
* **Element type:** the elements are BYTE or ADDRESS, and an element behaves
  exactly like a variable of that type in expressions and conversions.
* **Using an element:** `BUF(I)` is an element wherever a variable can
  appear: in expressions and as an assignment target, including multiple
  assignment (`BUF(I), BUF(J), N = 0;`).
* **Subscripts:** any expression, BYTE or ADDRESS. A BYTE subscript is
  zero-extended. **Subscripts are not checked:** as in PL/M-80, `BUF(80)`
  silently reads or writes the memory after the array.
* **Unsubscripted name:** a plain `BUF` means `BUF(0)`.
* **Not allowed:** arrays can't be parameters, and procedures can't return
  them. Pass an element (its value), or keep the array global, or in an
  outer procedure that the procedure can see.
* **Built-ins:** three PL/M-80 functions describe a variable, as constants
  worked out by the compiler:
  * `LENGTH(A)`: the number of elements
  * `LAST(A)`: the highest subscript, `LENGTH(A) - 1`
  * `SIZE(X)`: the number of bytes

  `LENGTH` and `LAST` are **BYTE** when the value is 255 or less and ADDRESS
  otherwise, as in PL/M-80. Arithmetic with them follows the BYTE rules: if
  `LENGTH(B)` is 10, then `LENGTH(B) + 250` wraps to 4. `SIZE` is always an
  ADDRESS.

  For a scalar, `LENGTH` and `LAST` give 1 and 0. The built-ins are only
  used when you haven't declared a procedure or variable with the same name
  yourself. They also accept structure references; see [Structures].

```
I = 0;
DO WHILE I < LENGTH(BUF);
    BUF(I) = ' ';
    I = I + 1;
END;
```

## Structures

```
DECLARE AIRPLANE (20) STRUCTURE (SPEED BYTE, ALTITUDE ADDRESS);
DECLARE PAYROLL (100) STRUCTURE (
    LAST$NAME (15) BYTE,
    FIRST$NAME (15) BYTE,
    DOLLARS ADDRESS);

AIRPLANE(17).ALTITUDE = 9000;
PAYROLL(N).FIRST$NAME(K) = 'A';
```

A **structure** groups members of different types under one name.
`STRUCTURE (member, ...)` is a type, written where `BYTE` or `ADDRESS` would
be, so it can be combined with the other parts of a declaration:

* **Forms:**
  * a single structure: `DECLARE POINT STRUCTURE (X BYTE, Y BYTE);`
  * an array of structures: `DECLARE AIRPLANE (20) STRUCTURE (...)`
  * factored: `DECLARE (A, B) STRUCTURE (...)`, which gives both the same
    members
  * BASED: `DECLARE ITEM BASED P STRUCTURE (...)`
* **Members:** each is `name [(dimension)] BYTE` or `ADDRESS`.
  * A member can be an array.
  * A member can't be factored, BASED, or itself a structure; structures
    don't nest.
  * Member names belong to their structure, so different structures can use
    the same names, and a member name never clashes with a variable.
* **Layout:** the members are stored one after another in the order
  written, so `SIZE` of a structure is the sum of its members' sizes. An
  array of structures stores whole structures one after another.

**References.** A member is referenced as `X.M`, `X(i).M`, `X.M(j)` or
`X(i).M(j)`. Each subscript can be any expression, and each is
**unchecked**, as for arrays. A member reference can be used anywhere a
variable can: in expressions, as an assignment target, and with `.`. An
unsubscripted member array (`POINT.COORD`) means its element 0.

A whole structure (`POINT`, or `AIRPLANE(3)`) is not a value. It can only be
used in these places:

| Reference | Meaning |
|---|---|
| `.POINT`, `.AIRPLANE(3)`, `.AIRPLANE` | the address of the structure (of element 0 for an array) |
| `.PAYROLL(5).FIRST$NAME` | the address of a member array |
| `LENGTH(AIRPLANE)`, `LAST(AIRPLANE)` | the number of structures in an array of structures, and the last subscript |
| `SIZE(POINT)`, `SIZE(AIRPLANE)`, `SIZE(AIRPLANE(3))` | bytes in a structure, the whole array, or one element |
| `LENGTH(PAYROLL(5).FIRST$NAME)`, `SIZE(...)` | a member array's length and size |

**The built-in shorthand.** A member of an *array* of structures normally
needs the array's subscript (`AIRPLANE.SPEED` is an error). The built-ins
are the exception, because every element's member array has the same size:
`LENGTH(PAYROLL.FIRST$NAME)` is 15.

**Pointers.** Structures combine naturally with BASED variables and the dot
operator. For example, a procedure can take a structure's address and look
at it through a BASED structure:

```
TOTAL: PROCEDURE (Q) ADDRESS;
    DECLARE Q ADDRESS, R BASED Q STRUCTURE (KEY BYTE, INFO (3) ADDRESS);
    ...  R.INFO(K) ...
END TOTAL;

X = TOTAL(.RECORD);
```

A linked list can be built in an array of structures with an ADDRESS member
holding `.NODE(i)`, and walked with a BASED structure.

**Not allowed:** structures can't be parameters, values, or iterative DO
indexes. Pass `.X` and use a BASED structure instead.

## BASED variables and the dot operator

```
DECLARE (NORTH$ERROR, EAST$ERROR) BYTE;
DECLARE ERROR$POINTER ADDRESS;
DECLARE ERROR BASED ERROR$POINTER BYTE;

ERROR$POINTER = .NORTH$ERROR;
ERROR = 0;                          /* clears NORTH$ERROR */
```

A **BASED variable** has no storage of its own. Each reference to it uses
the memory whose address is, *at that moment*, in its **base**.

* **Declaring:** `BASED base` must come right after the variable's name:
  * `DECLARE ITEM BASED PTR BYTE;`
  * `DECLARE (A BASED P, B BASED Q) ADDRESS;`
  * `DECLARE (LIST BASED LP) (100) ADDRESS;` (the parentheses are
    optional)
* **The base:** an ADDRESS scalar declared earlier. It can't be an array,
  an element, or itself BASED. Parameters and locals make good bases.
* **Arrays:** a BASED array's element `A(i)` is at `base + i` (BYTE) or
  `base + 2*i` (ADDRESS), and `A` alone means `A(0)`. As with ordinary
  arrays nothing is range-checked, so `DECLARE (BUF BASED P) (1) BYTE` can
  index any amount of memory.
* **Not allowed:** a BASED variable can't be a parameter or the index of an
  iterative DO.

The **dot operator** gives an ADDRESS value:

| Form | Value |
|---|---|
| `.X` | the address of variable `X` (of element 0 if `X` is an array) |
| `.A(i)` | the address of element `A(i)` |
| `.B` for a BASED `B` | the current value of its base (plus the offset for `.B(i)`) |
| `.P` | the entry address of procedure `P`. `P` must be declared at the outer level, and is written without arguments |
| `.(constants)` | the address of the listed bytes, stored with the program: numbers from 0 to 255 and strings of any length up to 255, as in `.('HELLO', 13, 0)` |

Together they give PL/M its pointers: pass `.BUF` to a procedure whose
parameter is the base of a BASED array, walk a string by adding 1 to a
base, or keep tables of addresses. The 6809 stores ADDRESS values high byte
first, so a BYTE BASED on an ADDRESS variable sees its high byte.

In an `--os9` module, `.X` gives the variable's actual run-time address
inside the process's data area, and constant lists live in the module.

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
| Assignment | `X = expression;` or `A(i) = expression;` |
| Multiple assignment | `X, A(i), Z = expression;` (evaluated once, converted for each target) |
| Call | `CALL NAME;` or `CALL NAME(arg, ...);` (untyped procedures only) |
| Conditional | `IF condition THEN statement; [ELSE statement;]` |
| Block | `[LABEL:] DO; declarations statements END [LABEL];` |
| Loop | `[LABEL:] DO WHILE condition; statements END [LABEL];` |
| Counted loop | `[LABEL:] DO I = start TO limit [BY step]; statements END [LABEL];` (see [Iterative DO]) |
| Case | `[LABEL:] DO CASE expression; statement-0; statement-1; ... END [LABEL];` (see [DO CASE]) |
| Return | `RETURN;` or `RETURN expression;` (inside procedures only) |
| Halt | `HALT;` stops the program through the hooks' `PLM_HALT` |
| Empty | `;` |
| Jump | `GOTO label;` or `GO TO label;` (see [Labels and GOTO]) |

* **ELSE:** an `ELSE` belongs to the nearest `IF` that doesn't already have
  one. To put several statements under `THEN` or `ELSE`, wrap them in
  `DO; ... END;`.
* **Labels:** any executable statement may carry labels, `L1: L2: stmt`.
  `END name` after a `DO` block must match one of the DO's labels. See
  [Labels and GOTO].
* **Loop and case blocks:** `DO WHILE`, iterative `DO` and `DO CASE` blocks
  may not contain declarations at their top level. A simple `DO` block inside
  them may.

## Labels and GOTO

```
        I = 0;
AGAIN:  I = I + 1;
        IF I < 10 THEN GOTO AGAIN;

        DO WHILE 1;
            IF DONE THEN GOTO FINISHED;     /* leave the loop */
            ...
        END;
FINISHED: ;
```

**Labels** (PL/M-80 manual 5.3, 9.3):

* **Where:** a label can be put on any executable statement, and a statement
  can have several (`L1: L2: N = 0;`). A labeled null statement `L: ;` marks
  a place, for example just before an `END`. Declarations and procedures
  can't be labeled.
* **Scope:** a label belongs to the **smallest block** that contains it.
  Every `DO` block counts as a block (simple, WHILE, iterative and CASE), and
  so does every procedure, but an `IF` does not.
  * A label may be used **before** it is defined.
  * The same name can be a label in two different blocks.
  * A label shares the block's names with its variables and procedures, so
    a clash is an error.
* **Labels on a DO:** a label on a `DO` statement belongs to the enclosing
  block, not to the DO's own block. So `GOTO L` to the label of
  `L: DO; ... END;` runs the whole block again.
* **The program's name:** the label on the program's first `DO` is outside
  every block, and can't be a GOTO target.

**GOTO** transfers control to a label that is visible from where the GOTO is:

* **Allowed:** a label in the same block, or in any block that encloses it.
  This lets you leave loops, `DO CASE` blocks and nested blocks early.
* **Not allowed:** jumping **into** a block. Its labels aren't visible from
  outside, so this is reported as `L is not declared`.
* **Leaving a procedure:** a GOTO in a procedure may jump out of it, even
  through several nested calls, but **only to a label at the outer level of
  the main program**. Anything else is an error. plmc then discards
  everything those calls left on the stack, which is safe because procedures
  keep their parameters and locals in fixed storage.

`GO TO`, written as two words, is the same as `GOTO`.

## Iterative DO

```
DO I = 1 TO 10;
    TOTAL = TOTAL + I;
END;

DO I = 1 TO 2 * N - 1 BY 2;     /* odd numbers */
    PROD = PROD * I;
END;
```

The index `I` must be a scalar variable, BYTE or ADDRESS (not an array
element). The loop works exactly as described in the PL/M-80 manual:

1. **Start:** `start` is evaluated **once**, converted to the index's type,
   and assigned to the index.
2. **Test:** `limit` is evaluated and converted to the index's type. If the
   index is **greater than** the limit, the loop ends.
3. **Body:** the statements in the block run.
4. **Step:** `step` (1 if there's no `BY`) is evaluated and converted to the
   index's type, and added to the index. If the addition **wraps around**
   past 255 (BYTE) or 65535 (ADDRESS), the loop ends. Otherwise it goes back
   to step 2.

Things that follow from these rules:

* **Re-evaluation:** the limit and the step are evaluated on *every* pass, so
  changing a variable they use inside the body changes the loop.
* **Final value:** after a normal exit the index is the first value past the
  limit. `DO I = 1 TO 10` leaves `I` = 11.
* **Wrap-around:** a BYTE loop `DO I = 0 TO 255` runs 256 times and ends
  because the increment wraps, leaving `I` = 0. Without the wrap rule it
  would never end.
* **Conversions:** these can surprise you. With a BYTE index, a limit of 300
  becomes 44.
* **No counting down:** arithmetic is unsigned, so `BY -1` on a BYTE index
  means `BY 255`, and a start above the limit runs the body zero times.
* **Step of zero:** this gives an endless loop unless the limit is passed
  some other way.

## DO CASE

```
DO CASE SCORE;
    ;                               /* 0: nothing */
    CONVERSIONS = CONVERSIONS + 1;  /* 1 */
    SAFETIES = SAFETIES + 1;        /* 2 */
    DO;                             /* 3: several statements */
        FIELDGOALS = FIELDGOALS + 1;
        POINTS = POINTS + 3;
    END;
END;
```

* **Choosing a case:** the selector expression is evaluated once, and
  statement number *selector* runs, counting from **0**. Exactly one
  statement runs, then control continues after `END`.
* **Null and grouped cases:** a null statement `;` is a case that does
  nothing. Use a `DO; ... END;` block to make one case of several statements.
* **Out of range:** a selector of the number of cases or more is undefined in
  PL/M. **plmc runs no case**: it range-checks the selector and skips the
  block.

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
| variable, or array element | its declared type |
| structure member `X.M`, `X(i).M(j)` | the member's type |
| `LENGTH(A)`, `LAST(A)` | BYTE if 255 or less, otherwise ADDRESS |
| `SIZE(X)` | ADDRESS |
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
| an implicit dimension, `DECLARE X(*) BYTE` (only meaningful with `INITIAL`/`DATA`) | `an implicit dimension '(*)' is not supported` |
| `LABEL` declarations | `... is not supported` |
| `INITIAL` and `DATA` | `initialized data is not supported` |
| `REENTRANT`, `INTERRUPT`, `PUBLIC` | `... is not supported` |
| `PLUS` and `MINUS` (add and subtract with carry) | `PLUS/MINUS (add/subtract with carry) is not supported` |
| embedded assignment `:=` | `embedded assignment ':=' is not supported` |
| `ENABLE`, `DISABLE` | `ENABLE/DISABLE is not supported` |
| strings of three or more characters | `strings longer than 2 characters are not supported` |

Of the PL/M-80 built-in procedures, only `LENGTH`, `LAST` and `SIZE` are
provided.
Others such as `HIGH`, `LOW`, `SHL`, `SHR`, `ROL` and `ROR` are not
predefined. Using one gives `... is not declared`, unless you write it
yourself as a procedure or an `EXTERNAL`.

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
| `X is not a procedure` | `CALL X` where X is a variable |
| `X is not a procedure or an array` | `X(...)` in an expression where X is a scalar variable |
| `X is not an array` | `X(i) = ...` where X is a scalar |
| `array A takes exactly one subscript` / `an array takes exactly one subscript` | `A(i, j)` in an expression / as a target |
| `array dimension must be from 1 to 65535`, `expected an array dimension (a number), found ...` | dimension 0, or not a number |
| `array X is too large (more than 65535 bytes)` | e.g. `DECLARE X(40000) ADDRESS` |
| `parameter X cannot be an array` | a parameter declared with a dimension |
| `LENGTH needs the name of a variable` (or `LAST`, `SIZE`) | argument isn't a declared variable |
| `structure X cannot be used as a value; select a member, as in X.M` | a whole structure, or a structure array's element, as a value or target |
| `X is not a structure`, `structure X has no member M` | bad member reference |
| `X is an array of structures: select one, as in X(i).M` | `X.M` without the array's subscript (outside the built-ins) |
| `X is not an array`, `X.M is not an array` | a subscript on something that isn't an array |
| `structures cannot be nested ...`, `structure members cannot be factored`, `a structure member cannot be BASED`, `member M is declared twice` | bad member list |
| `parameter X cannot be a structure` | a structure declared as a parameter |
| `literal X expands recursively (more than 32 levels)` | a literal whose text uses itself |
| `a LITERALLY declaration cannot be factored`, `... cannot have a dimension` | `DECLARE (A, B) LITERALLY ...`, `DECLARE A(3) LITERALLY ...` |
| `expected a string, found ...` | `LITERALLY` not followed by a string |
| `strings are limited to 255 characters` | a string over 255 characters |
| `strings longer than 2 characters are not supported` | a long string used as a value |
| `the index of an iterative DO must be a scalar variable` | `DO A = ...` where A is an array, a procedure, BASED, or undeclared |
| `L is not a label` | `GOTO X` where X is a variable or procedure |
| `a GOTO out of a procedure must go to a label at the outer level of the main program` | the target is in an enclosing procedure, or in a block of the main program |
| `a label must be followed by a statement (use 'L: ;')` | a label right before `END` |
| `L is a label, not a variable or procedure`, `cannot assign to label L` | a label used as a value |
| `P, the base of X, is not declared` | `BASED` names an undeclared base |
| `the base of a BASED variable must be an ADDRESS scalar that is not itself BASED` | base is BYTE, an array, a procedure, or BASED |
| `parameter X cannot be BASED`, `a LITERALLY declaration cannot be BASED` | `BASED` in the wrong place |
| `the dot operator needs a variable, an array element or a procedure` | e.g. `.LENGTH(A)` |
| `the address of a procedure needs a procedure declared at the outer level, written without arguments` | `.P` for a nested procedure, or `.P(1)` |
| `a number in a constant list must fit in a byte`, `expected a number or a string, ...` | bad item in `.(...)` |
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
| `plmc: --org and --vectors cannot be used with --os9 ...` | OS-9 modules have no fixed address |
| `plmc: --stack is only used with --os9`, `--name is only used with --os9` | option without `--os9` |
| `plmc: bad --stack size ...`, `bad --name ...` | stack below 32 bytes, or an invalid module name |

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
  **In `--os9` modules, U must be preserved too**, because it points at the
  data area, and hooks must themselves be position-independent (see
  [OS-9 modules]).
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

All four hooks files implement the same procedures, so a program that uses
only these runs unchanged on any of the targets:

```
PUTCH:  PROCEDURE (C) EXTERNAL;    DECLARE C BYTE;    END PUTCH;
PUTDEC: PROCEDURE (N) EXTERNAL;    DECLARE N ADDRESS; END PUTDEC;
GETCH:  PROCEDURE BYTE EXTERNAL;                      END GETCH;
EXIT:   PROCEDURE (CODE) EXTERNAL; DECLARE CODE BYTE; END EXIT;
```

`PUTDEC` prints a number in decimal followed by a newline.

| | `runtime/sim` | `runtime/coco` | `runtime/os9` | `runtime/example` |
|---|---|---|---|---|
| `PUTCH` | simulator I/O port | Color BASIC CHROUT; LF sent as CR | `I$Write` to path 1; LF sent as CR | MC6850 ACIA, waits until ready to send |
| `GETCH` | stdin; 0 at end of input | waits for a key (POLCAT); ENTER gives 13 | `I$Read` from path 0; 0 at end of file | ACIA, waits for a character |
| `PUTDEC` | simulator decimal port | via `RT_DIV16` and CHROUT | via `RT_DIV16`, one `I$Write`, ends with CR | via `RT_DIV16`, ends with CR LF |
| `EXIT`, end of program | simulator exit status | back to BASIC (status discarded) | `F$Exit` with the status | stops in a loop |
| `HALT` | exit status 0 | back to BASIC | `F$Exit` with status 0 | stops in a loop |
| extra | | `CLS` clears the screen | | |
| stack | `$FF00` | BASIC's | in the data area (`--stack`) | `$8000` |

## Writing hooks for a new machine

Copy `runtime/example/plm_hooks.asm` to a directory of your own, change the
I/O routines and `RT_STACKTOP`, and assemble with `-I runtime -I yourdir`.
Add any extra routines your programs need as further labels, and declare them
in PL/M as `EXTERNAL`. The example file's `ACIA_BASE` and `RT_STACKTOP` are
wrapped in `IFNDEF`, so they can also be overridden from the lwasm command
line with `-D`.

# OS-9 modules

With `--os9`, plmc produces an **OS-9 program module** for Microware OS-9 and
NitrOS-9, Level 1 or Level 2. Like every OS-9 command, the module can be
loaded at any address and shared by several processes at once.

```
build/plmc --os9 -o sum.asm sum.plm
lwasm -9 -f os9 -I runtime -I runtime/os9 -o sum sum.asm
```

`lwasm -f os9` writes the finished module, with its header parity and CRC.
Copy it to the `CMDS` directory of an OS-9 disk (with toolshed:
`os9 copy sum disk.dsk,CMDS/sum` and `os9 attr -e -pe disk.dsk,CMDS/sum`),
then run it from the shell like any other command:

```
OS9: sum
55
OS9: sum >/d0/result
```

`sim6809 --os9 sum` runs the module without OS-9, by emulating the system
calls the hooks use (see the sim6809 manual).

## What `--os9` changes {#os9-changes}

**Position-independent code.** Calls are `LBSR` rather than `JSR`, and jumps
are `LBRA` rather than `JMP`. Every other branch is already relative, so the
module contains no absolute addresses.

**Data in the process's data area.** OS-9 gives each process its own data
area and points U at it.

* **Variables:** declared as `RMB` offsets inside the module (which lwasm
  allocates in the data area, not the module), and every access is
  U-relative, as in `STB V_I,U`. Because the module never writes to itself,
  it is reentrant.
* **Order in the source:** variables come first, so lwasm knows their
  offsets and uses short indexed forms.
* **Hooks and stack:** the hooks' own buffers and the stack reserve follow
  the code.

**Startup.** `runtime/plm_crt0_os9.asm` keeps the stack, U and DP that OS-9
set up, calls the program, and ends with `F$Exit`.

**Module header.**

| Field | Value |
|---|---|
| type/language | `$11` (Prgrm+Objct) |
| attributes/revision | `$81` (ReEnt, revision 1) |
| edition | 1 |
| data size | variables + hook buffers + `--stack` bytes |
| name | the PL/M program name, or `--name` |

The beginning of the `SUM` example compiled with `--os9`:

```
        PRAGMA  6809
        MOD     RT_EOM,RT_NAME,$11,$81,RT_START,RT_SIZE

; variables (data area, U-relative)
V_I     RMB     1
V_TOTAL RMB     2

RT_NAME FCS     /SUM/
        FCB     1               ; edition
        INCLUDE "plm_crt0_os9.asm"

; program SUM
PLM_MAIN
; 4: TOTAL = 0;
        LDB     #$00
        CLRA
        STD     V_TOTAL,U
...
RT_STACK RMB    256             ; stack space (--stack)
RT_SIZE EQU     .               ; data area size
        EMOD
RT_EOM  EQU     *
```

## Module names

OS-9 finds a command by the **module name** inside the file, and expects it
to match the file name in `CMDS`. By default plmc uses the PL/M program name.

* **Matching the file name:** use `--name` to give the module the name of the
  file you'll install, for example `--name sum`.
* **Avoiding clashes:** if a module with the same name is already in memory
  (or in the boot file), OS-9 runs *that* module instead. A PL/M program
  called `ECHO`, for instance, would run NitrOS-9's own `echo` command.
  Choose a program name, or `--name`, that doesn't clash.

Module names are 1 to 29 characters: a letter followed by letters, digits,
`_`, `.` or `$`.

## The OS-9 hooks

`runtime/os9/plm_hooks.asm` provides `PUTCH`, `GETCH`, `PUTDEC` and `EXIT`.
They use these system calls:

| Hook | System call |
|---|---|
| `PUTCH`, `PUTDEC` | `I$Write` on path 1 (standard output) |
| `GETCH` | `I$Read` on path 0 (standard input) |
| `EXIT`, end of program, `HALT` | `F$Exit`, which gives the shell the exit status |

* **Newlines:** OS-9 ends lines with CR, so `PUTCH(10)` is sent as CR and
  `PUTDEC` ends its number with CR. Terminals add the line feed, and files
  get standard OS-9 text. Programs written for the simulator therefore print
  the same way under OS-9.
* **End of input:** `GETCH` returns 0 at end of file (`E$EOF`).
* **Errors:** any other I/O error ends the program with the OS-9 error code
  as its exit status, which the shell reports as `ERROR #nnn`.
* **Redirection:** standard input and output can be redirected as usual, for
  example `sum <data >/d0/out`.

**Rules for your own OS-9 hooks:**

* **Calls and jumps:** use only relative ones (`BSR`/`LBSR`/`BRA`/`LBRA`, or
  `JSR label,PCR`).
* **Data:** put it in `RMB` storage and address it through U (`LEAX buf,U`).
* **Registers:** preserve U and S.
* **System calls:** use `OS9 F$xxx`, which lwasm assembles as `SWI2` plus the
  function code.

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

With `--os9` the layout is that of an OS-9 module instead; see
[What `--os9` changes](#os9-changes).

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
* **Iterative DO:**
  * The start value is stored in the index.
  * At the top of the loop the index is compared with the limit, which is
    re-evaluated there (`LBHI`/`LBLO` to the exit).
  * After the body the step is added with `ADDB`/`ADDD`, and the result
    stored. `LBCC` goes back to the top; a carry means the addition wrapped,
    and the loop ends.
* **DO CASE:**
  * The selector is range-checked (`CMPD #n` / `LBHS` past the block), then
    doubled.
  * `LEAX table,PCR` / `LDD D,X` / `JMP D,X` jumps through a table of
    `FDB case-table` *offsets*. Offsets rather than addresses keep the code
    position-independent, so the same sequence works in `--os9` modules.
  * Each case ends with `LBRA` past the block.
* **GOTO:**
  * Every statement leaves the stack as it found it, so a GOTO within a
    procedure, or within the main program, is just `LBRA`.
  * If any GOTO leaves a procedure, `PLM_MAIN` saves its stack pointer in
    `RT_MAINS` on entry (`STS RT_MAINS`). Such a GOTO is
    `LDS RT_MAINS` / `LBRA label`, which discards the return addresses and
    intermediate values of the calls it leaves.
* **BASED variables:**
  * The base is loaded into X (`LDX P`, or `LDX P,U`), and the variable is
    used as `,X` or `k,X`, so a store needs no extra registers.
  * Computed subscripts use the same `D,X` sequence as ordinary arrays, with
    the base's value as X.
* **Dot operator:**
  * Normally `LDD #V_X`. In `--os9` modules it is `LEAX V_X,U` / `TFR X,D`
    for a variable, and `LEAX P_X,PCR` / `TFR X,D` for a procedure, so the
    code stays position-independent.
  * Constant lists become `FCB` data after the procedures.
* **Structures:**
  * A member reference whose subscripts are all constants is a fixed
    location, `V_X+offset` (structure subscript × structure size + member
    offset + member subscript × member size), and is used directly as an
    operand.
  * Computed subscripts are scaled with shifts when the size is a power of
    two, and with `RT_MUL16` otherwise. Two computed subscripts are added on
    the stack.
  * BASED structures and the dot operator use the same paths as arrays.
* **Array elements:**
  * *Constant subscript:* the element is an ordinary operand, `V_A+6` (or
    `V_A+6,U` in an OS-9 module).
  * *Computed subscript:* the subscript is computed into D and doubled for
    ADDRESS arrays. The array's address goes into X (`LDX #V_A`, or
    `LEAX V_A,U`), and the element is loaded with `LDB D,X` / `LDD D,X`.
  * *Storing to a computed element:* the value is pushed first, then the
    address is formed with `LEAX D,X`, and the value is pulled and stored
    through X.
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
  | `G_X`, `Gn_X` | statement label `X` (in scope *n*) |
  | `Lnnn` | compiler-generated label |
  | `RT_*` | runtime routine or data |
  | `PLM_*` | entry point the hooks provide |
  | `X` | `EXTERNAL` procedure `X` |

# Inspecting the compiler's view

`--tokens` prints each token with its line and column. Numbers are shown with
their value, which is useful for checking how a constant was read. Strings
are printed as `STRING 'text'`, with the value of one- and two-character
strings. `--tokens` shows the source as written; LITERALLY names are expanded
later, by the parser.

```
2:1 IDENT X
2:3 PUNCT =
2:5 NUMBER 255
2:10 PUNCT +
```

`--ast` prints the syntax tree after parsing, and after LITERALLY substitution,
with expressions written as
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
| `run` | complete programs compiled, assembled and run in `sim6809`; output and exit status compared. They cover every operator and conversion rule, control flow including iterative DO (wrap-around, re-evaluated limit and step, conversions) and DO CASE, input, procedures (including nested calls), arrays (a sieve, a sort, edge cases), structures (the manual's AIRPLANE, PAYROLL, POINT and 100×100 examples, built-ins on every reference form, BASED structures, a linked list), BASED variables and the dot operator (the manual's examples, BASED arrays and parameters, constant lists), labels and GOTO (out of loops, blocks and nested procedure calls; 10000 jumps out of three calls to check the stack is reset), `HALT`, `--org` and `--vectors` |
| `fuzz` | random programs with thousands of random expressions, array elements (with computed subscripts), element assignments, iterative DO loops over random bounds and steps, DO CASE, pointers (`.BA(k)` with reads and writes through BASED variables and BASED arrays), and members of an array of 5-byte structures, whose subscripts need a multiply, checked against an independent C model of the language rules; `make fuzz-test N=2000` runs more |
| `coco`, `xroar` | the Color Computer hooks, in the simulator and on an emulated CoCo 2 (the `xroar` suite is skipped if XRoar, the ROMs or toolshed aren't installed) |
| `os9` | every `run` program plus 25 fuzz programs compiled with `--os9` and run by `sim6809 --os9`, each at two different module and data addresses with memory protection on. Also checks that the generated code has no absolute calls, jumps or variable references, and (with toolshed) that `os9 ident` accepts the module |
| `nitros9` | the `run` programs as modules on a NitrOS-9 Level 2 disk, booted on an emulated CoCo 3 under XRoar, with each program's output compared. Uses `$NITROS9_DISK` (a bootable NitrOS-9 Level 2 disk image) and `$COCO3_ROM`; skipped if they, XRoar or toolshed are missing |
| `sim` | the simulator itself (see the sim6809 manual) |

# Source code

| File | Contents |
|---|---|
| `src/main.c` | command line, file handling |
| `src/lexer.c` | tokens, numbers, strings, comments |
| `src/parser.c`, `ast.c` | recursive-descent parser, syntax tree, `--ast` |
| `src/symtab.c`, `sema.c` | scopes, name resolution, types, call checks, recursion detection |
| `src/codegen.c` | assembly output |
| `runtime/plm_crt0.asm`, `plm_crt0_os9.asm`, `plm_rt.asm` | startup code (normal and OS-9), 16-bit multiply and divide |
| `runtime/*/plm_hooks.asm` | hooks for the simulator, the CoCo, OS-9, and the ACIA template |
