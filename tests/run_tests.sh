#!/usr/bin/env bash
# plm09 test runner.
#   run_tests.sh sim|lex|parse|err|run|coco|xroar   run one suite
#   run_tests.sh os9 [N]                   run tests + N fuzz seeds as OS-9 modules
#   run_tests.sh flex [N]                  run tests + N fuzz seeds as FLEX commands
#   run_tests.sh dragonflex                run tests on real FLEX (Dragon 64, XRoar)
#   run_tests.sh fuzz [N]                  random expression tests
#   run_tests.sh one FILE.plm              compile, assemble, simulate one program
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=$ROOT/build
PLMC=$BUILD/plmc
SIM=$BUILD/sim6809
OUT=$BUILD/tests
LWASM=(lwasm -9 -f srec -I "$ROOT/runtime" -I "$ROOT/runtime/sim")

pass=0
fail=0
mkdir -p "$OUT"

ok()  { pass=$((pass + 1)); }
bad() { fail=$((fail + 1)); echo "FAIL $1: $2"; }

# assemble ASM -> S19; listing alongside
assemble() {
    "${LWASM[@]}" --list="${2%.s19}.lst" -o "$2" "$1" 2>"$2.err"
}

# compare actual file $2 with expected $3; name $1
check_diff() {
    if diff -u "$3" "$2" >"$2.diff" 2>&1; then
        return 0
    fi
    bad "$1" "output differs (see $2.diff)"
    head -20 "$2.diff"
    return 1
}

# (address, bytes) of each instruction from LEN_BEGIN up to LEN_END, from an
# lwasm listing ($1 = listing) or a sim6809 -t trace ($1 = trace)
len_pairs() {
    awk -v b="$2" -v e="$3" '$1 ~ /^[0-9A-F][0-9A-F][0-9A-F][0-9A-F]$/ &&
        $2 ~ /^[0-9A-F]+$/ && $1 >= b && $1 < e { print $1, $2 }' "$1"
}

# sim test: assemble, run, compare stdout+rc with .out.  Optional first-line
# "; SIMARGS: ..." passes extra simulator options.  os9_*.asm files are OS-9
# modules: assembled with -f os9 and run with --os9.  Files whose listing has
# LEN_BEGIN/LEN_END also get the trace-vs-listing instruction length check.
sim_one() {    # $1 = asm, $2 = expected .out
    local name s19 lst args b e
    name=sim/$(basename "$1" .asm)
    s19=$OUT/$(basename "$1" .asm).s19
    lst=${s19%.s19}.lst
    args=$(sed -n '1s/^; SIMARGS: *//p' "$1")
    case $(basename "$1") in
    os9_*)      # OS-9 module tests: assembled with -f os9, run with --os9
        if ! lwasm -9 -f os9 --list="$lst" -o "$s19" "$1" 2>"$s19.err"; then
            bad "$name" "lwasm failed"; cat "$s19.err"; return
        fi
        args="--os9 $args" ;;
    flex_*)     # FLEX tests: S-records converted to a .CMD file, run with --flex
        if ! assemble "$1" "$s19" || ! "$BUILD/srec2flex" "$s19" "${s19%.s19}.cmd" 2>>"$s19.err"; then
            bad "$name" "lwasm or srec2flex failed"; cat "$s19.err"; return
        fi
        s19=${s19%.s19}.cmd
        args="--flex $args" ;;
    *)
        if ! assemble "$1" "$s19"; then bad "$name" "lwasm failed"; cat "$s19.err"; return; fi ;;
    esac
    b=$(awk '/LEN_BEGIN/ { print $1; exit }' "$lst")
    e=$(awk '/LEN_END/ { print $1; exit }' "$lst")
    [ -n "$b" ] && args="$args -t"
    # shellcheck disable=SC2086
    "$SIM" $args "$s19" </dev/null >"$s19.stdout" 2>"$s19.stderr"
    echo "rc=$?" >>"$s19.stdout"
    check_diff "$name" "$s19.stdout" "$2" || return
    if [ -n "$b" ]; then
        len_pairs "$lst" "$b" "$e" >"$s19.want"
        len_pairs "$s19.stderr" "$b" "$e" >"$s19.got"
        if [ ! -s "$s19.want" ]; then bad "$name" "no instructions in listing range"; return; fi
        check_diff "$name (lengths)" "$s19.got" "$s19.want" || return
        echo "  $name: $(wc -l <"$s19.want") instructions match the lwasm listing"
    fi
    ok
}

suite_sim() {
    local f
    for f in "$ROOT"/sim/tests/*.asm; do
        [ -e "$f" ] || continue
        sim_one "$f" "${f%.asm}.out"
    done
    if python3 "$ROOT/sim/tests/gen_len_ops.py" >"$OUT/len_ops.asm"; then
        sim_one "$OUT/len_ops.asm" "$ROOT/sim/tests/len_ops.out"
    else
        bad sim/len_ops "generator failed"
    fi
}

suite_golden() {    # $1 = dir, $2 = plmc flag, $3 = expected extension
    local f name act
    for f in "$ROOT/tests/$1"/*.plm; do
        [ -e "$f" ] || continue
        name=$1/$(basename "$f" .plm)
        act=$OUT/$1-$(basename "$f" .plm).$3
        "$PLMC" "$2" "$f" >"$act" 2>&1
        check_diff "$name" "$act" "${f%.plm}.$3" && ok
    done
}

suite_err() {
    local f name act want args
    for f in "$ROOT"/tests/err/*.plm; do
        [ -e "$f" ] || continue
        name=err/$(basename "$f" .plm)
        act=$OUT/err-$(basename "$f" .plm).stderr
        args=$(sed -n '1s/^\/\* PLMCARGS: \(.*\) \*\/$/\1/p' "$f")
        # shellcheck disable=SC2086
        if "$PLMC" $args -o "$OUT/err.asm" "$f" 2>"$act" >/dev/null; then
            bad "$name" "compiled without error"; continue
        fi
        want=$(head -1 "${f%.plm}.err")
        if grep -qF -- "$want" "$act"; then ok; else
            bad "$name" "expected '$want' in:"; cat "$act"
        fi
    done
}

# compile+assemble+run; stdout plus "rc=N" goes to $2.  A first line
# "/* PLMCARGS: ... */" in the source passes extra compiler options.
build_and_run() {
    local base=${2%.stdout} args
    args=$(sed -n '1s/^\/\* PLMCARGS: \(.*\) \*\/$/\1/p' "$1")
    # shellcheck disable=SC2086
    "$PLMC" $args -o "$base.asm" "$1" 2>"$base.plmc.err" || { cat "$base.plmc.err"; return 10; }
    assemble "$base.asm" "$base.s19" || { cat "$base.s19.err"; return 11; }
    "$SIM" "$base.s19" <"${3:-/dev/null}" >"$2" 2>"$base.sim.err"
    echo "rc=$?" >>"$2"
}

suite_run() {
    local f name act exp
    for f in "$ROOT"/tests/run/*.plm; do
        [ -e "$f" ] || continue
        name=run/$(basename "$f" .plm)
        act=$OUT/run-$(basename "$f" .plm).stdout
        build_and_run "$f" "$act" "$( [ -e "${f%.plm}.in" ] && echo "${f%.plm}.in")" \
            || { bad "$name" "build failed"; continue; }
        exp=$OUT/run-$(basename "$f" .plm).expected
        cat "${f%.plm}.out" >"$exp"
        echo "rc=$(cat "${f%.plm}.rc" 2>/dev/null || echo 0)" >>"$exp"
        check_diff "$name" "$act" "$exp" && ok
    done
    # the example (real hardware) hooks must assemble with compiled code
    if [ -e "$OUT/run-hello.asm" ]; then
        if lwasm -9 -f srec -I "$ROOT/runtime" -I "$ROOT/runtime/example" \
                -o "$OUT/hello-example.s19" "$OUT/run-hello.asm" 2>"$OUT/hello-example.err"; then
            ok
        else
            bad run/example-hooks "lwasm failed with runtime/example hooks"
            cat "$OUT/hello-example.err"
        fi
    fi
}

# CoCo hooks in sim6809, against tests/coco/fakerom.asm (a stand-in for
# the Color BASIC ROM whose fake EXEC also checks the stack on return)
suite_coco() {
    local rom=$OUT/coco-fakerom.s19 f name base exp
    if ! lwasm -9 -f srec -o "$rom" "$ROOT/tests/coco/fakerom.asm" 2>"$rom.err"; then
        bad coco/fakerom "lwasm failed"; cat "$rom.err"; return
    fi
    for f in "$ROOT"/tests/coco/*.plm; do
        [ -e "${f%.plm}.out" ] || continue          # XRoar-only (vsync)
        name=coco/$(basename "$f" .plm)
        base=$OUT/coco-$(basename "$f" .plm)
        if ! "$PLMC" --org 3000H -o "$base.asm" "$f" 2>"$base.plmc.err"; then
            bad "$name" "plmc failed"; cat "$base.plmc.err"; continue
        fi
        if ! lwasm -9 -f srec -I "$ROOT/runtime" -I "$ROOT/runtime/coco" \
                -o "$base.s19" "$base.asm" 2>"$base.s19.err"; then
            bad "$name" "lwasm failed"; cat "$base.s19.err"; continue
        fi
        cat "$base.s19" "$rom" >"$base.both.s19"     # ROM last: its S9 entry wins
        "$SIM" "$base.both.s19" <"$( [ -e "${f%.plm}.in" ] && echo "${f%.plm}.in" || echo /dev/null)" \
            >"$base.stdout" 2>"$base.sim.err"
        echo "rc=$?" >>"$base.stdout"
        exp=$base.expected
        cat "${f%.plm}.out" >"$exp"
        echo "rc=$(cat "${f%.plm}.rc" 2>/dev/null || echo 0)" >>"$exp"
        check_diff "$name" "$base.stdout" "$exp" && ok
    done
}

# The same CoCo programs on an emulated CoCo 2 (XRoar, Disk Extended Color
# BASIC): each X.plm with an X.screen file is put on a disk image and run
# with  POKE&H2F00,57:LOADM"T":EXEC:EXEC&H2F00  (plus X.keys, if present,
# typed afterwards).  XRoar traps at $2F00, which is reached only if the
# program returned to BASIC, snapshots the machine, and the text screen is
# compared with X.screen.  Skipped when xroar, decb or the ROMs are missing.
suite_xroar() {
    local f name base marker keys
    for t in xroar decb; do
        command -v $t >/dev/null || { echo "xroar: $t not found (skipped)"; exit 0; }
    done
    if ! xroar -q -machine cocous -ui null -ao null -timeout 1 >/dev/null 2>&1; then
        echo "xroar: cannot start a cocous machine; missing ROMs? (skipped)"; exit 0
    fi
    for f in "$ROOT"/tests/coco/*.plm; do
        [ -e "${f%.plm}.screen" ] || continue
        name=xroar/$(basename "$f" .plm)
        base=$OUT/xroar-$(basename "$f" .plm)
        rm -f "$base.sna" "$base.dsk"
        if ! "$PLMC" --org 3000H -o "$base.asm" "$f" 2>"$base.plmc.err" ||
           ! lwasm -9 -f decb -I "$ROOT/runtime" -I "$ROOT/runtime/coco" \
                -o "$base.bin" "$base.asm" 2>"$base.err" ||
           ! decb dskini "$base.dsk" >/dev/null 2>&1 ||
           ! decb copy -2 -b "$base.bin" "$base.dsk,T.BIN" >/dev/null 2>&1; then
            bad "$name" "build failed"; cat "$base.plmc.err" "$base.err" 2>/dev/null; continue
        fi
        marker=$(od -An -tx1 -j5 -N8 "$base.bin" | tr -d ' \n')   # first 8 bytes at $3000
        keys=$(cat "${f%.plm}.keys" 2>/dev/null)
        timeout 120 xroar -q -machine cocous -ui null -ao null -no-ratelimit \
            -load-fd0 "$base.dsk" -type "POKE&H2F00,57:LOADM\"T\":EXEC:EXEC&H2F00\r$keys" \
            -trap pc=0x2F00 -trap-snap "$base.sna" -trap-timeout 0 -timeout 60 >/dev/null 2>&1
        if [ ! -e "$base.sna" ]; then
            bad "$name" "never returned to BASIC (no trap at \$2F00)"; continue
        fi
        python3 -I "$ROOT/tools/coco_screen.py" "$base.sna" "$marker" 3000 >"$base.screen"
        check_diff "$name" "$base.screen" "${f%.plm}.screen" && ok
    done
}

# Every run test (and some fuzz programs) again as OS-9 modules: compiled
# with --os9, assembled with -f os9 and the runtime/os9 hooks, and run by
# sim6809 --os9 at two different module and data addresses, which must give
# the same output.  The generated code must contain no absolute calls or
# jumps and no variable reference that isn't U-relative, and (if toolshed is
# installed) "os9 ident" must accept the module.
OS9_PLACES=("" "--load 0x3123 --data 0x9E00")

os9_build() {     # $1 = source, $2 = base path, [$3 = module name]; module in $2.mod
    # --memory: room for MEMORY (the memory test uses 100 bytes of it)
    "$PLMC" --os9 --memory 128 ${3:+--name "$3"} -o "$2.asm" "$1" 2>"$2.plmc.err" || { cat "$2.plmc.err"; return 1; }
    lwasm -9 -f os9 -I "$ROOT/runtime" -I "$ROOT/runtime/os9" \
        --list="$2.lst" -o "$2.mod" "$2.asm" 2>"$2.err" || { cat "$2.err"; return 1; }
}

os9_check_pic() {   # $1 = name, $2 = generated .asm
    # absolute = operand without an index register (JMP D,X is fine)
    if grep -nE '^[[:space:]]+(JSR|JMP)[[:space:]]+[^,[:space:]]+([[:space:]]|$)' "$2" >"$2.pic"; then
        bad "$1" "absolute JSR/JMP in --os9 output"; cat "$2.pic"; return 1
    fi
    if grep -nE '^[[:space:]]+[A-Z]+[[:space:]]+#?V[0-9]*_[A-Z0-9]+(\+[0-9]+)?([[:space:]]|$)' "$2" >"$2.pic"; then
        bad "$1" "variable reference that is not U-relative"; cat "$2.pic"; return 1
    fi
    if grep -nE '^[[:space:]]+[A-Z]+[[:space:]]+M[0-9]*_[A-Z0-9]+(\+[0-9]+)?,' "$2" >"$2.pic"; then
        bad "$1" "fixed-address (AT) variable reference that is relative"; cat "$2.pic"; return 1
    fi
    if command -v os9 >/dev/null && ! os9 ident "${2%.asm}.mod" 2>&1 | grep -q 'CRC.*(Good)'; then
        bad "$1" "os9 ident does not accept the module"; return 1
    fi
}

os9_run() {    # $1 = name, $2 = module, $3 = input, $4 = expected; all places
    local place out=${2%.mod}.stdout
    for place in "${OS9_PLACES[@]}"; do
        # shellcheck disable=SC2086
        "$SIM" --os9 $place "$2" <"$3" >"$out" 2>"$out.err"
        echo "rc=$?" >>"$out"
        check_diff "$1 ${place:-(default addresses)}" "$out" "$4" || return 1
    done
}

suite_os9() {
    local f name base exp n=${1:-25} seed gen=$BUILD/exprgen
    for f in "$ROOT"/tests/run/*.plm; do
        [ -e "$f" ] || continue
        grep -q '^/\* NOT-OS9:' "$f" && continue      # e.g. INTERRUPT procedures
        name=os9/$(basename "$f" .plm)
        base=$OUT/os9-$(basename "$f" .plm)
        os9_build "$f" "$base" || { bad "$name" "build failed"; continue; }
        os9_check_pic "$name" "$base.asm" || continue
        # --org/--vectors/AT-a-number tests: build and check only
        grep -q '^/\* PLMCARGS:' "$f" && { ok; continue; }
        exp=$base.expected
        cat "${f%.plm}.out" >"$exp"
        echo "rc=$(cat "${f%.plm}.rc" 2>/dev/null || echo 0)" >>"$exp"
        os9_run "$name" "$base.mod" "$( [ -e "${f%.plm}.in" ] && echo "${f%.plm}.in" || echo /dev/null)" "$exp" && ok
    done
    cc -std=c11 -O2 -Wall -Wextra -Werror -o "$gen" "$ROOT/tests/gen/exprgen.c" || { bad os9/fuzz "generator build"; return; }
    for seed in $(seq 1001 $((1000 + n))); do
        base=$OUT/os9-fuzz
        "$gen" "$seed" "$base.plm" "$base.expected"
        echo "rc=0" >>"$base.expected"
        if ! os9_build "$base.plm" "$base"; then
            bad "os9/fuzz/$seed" "build failed"
        elif os9_check_pic "os9/fuzz/$seed" "$base.asm" &&
             os9_run "os9/fuzz/$seed" "$base.mod" /dev/null "$base.expected"; then
            ok
            continue
        fi
        cp "$base.plm" "$OUT/os9-fuzz-fail-$seed.plm"
        echo "  program kept as $OUT/os9-fuzz-fail-$seed.plm"
        break
    done
}

# The run tests (and fuzz seeds) as FLEX commands: assembled with the FLEX
# hooks, converted to a .CMD file by srec2flex, and run by sim6809 --flex,
# which emulates the FLEX DOS entry points and protects FLEX's memory.
# Tests with PLMCARGS (fixed addresses, vectors, interrupts) are skipped.
flex_build() {     # $1 = source, $2 = base path; command in $2.cmd
    "$PLMC" -o "$2.asm" "$1" 2>"$2.plmc.err" || { cat "$2.plmc.err"; return 1; }
    lwasm -9 -f srec -I "$ROOT/runtime" -I "$ROOT/runtime/flex" \
        --list="$2.lst" -o "$2.s19" "$2.asm" 2>"$2.err" || { cat "$2.err"; return 1; }
    "$BUILD/srec2flex" "$2.s19" "$2.cmd" 2>>"$2.err" || { cat "$2.err"; return 1; }
}

flex_run() {    # $1 = name, $2 = command, $3 = input, $4 = expected
    local out=${2%.cmd}.stdout
    "$SIM" --flex "$2" <"$3" >"$out" 2>"$out.err"
    echo "rc=$?" >>"$out"
    check_diff "$1" "$out" "$4"
}

suite_flex() {
    local f name base exp n=${1:-25} seed gen=$BUILD/exprgen
    for f in "$ROOT"/tests/run/*.plm; do
        [ -e "$f" ] || continue
        grep -q '^/\* PLMCARGS:' "$f" && continue
        name=flex/$(basename "$f" .plm)
        base=$OUT/flex-$(basename "$f" .plm)
        flex_build "$f" "$base" || { bad "$name" "build failed"; continue; }
        exp=$base.expected
        cat "${f%.plm}.out" >"$exp"
        echo "rc=$(cat "${f%.plm}.rc" 2>/dev/null || echo 0)" >>"$exp"
        flex_run "$name" "$base.cmd" "$( [ -e "${f%.plm}.in" ] && echo "${f%.plm}.in" || echo /dev/null)" "$exp" && ok
    done
    cc -std=c11 -O2 -Wall -Wextra -Werror -o "$gen" "$ROOT/tests/gen/exprgen.c" || { bad flex/fuzz "generator build"; return; }
    for seed in $(seq 2001 $((2000 + n))); do
        base=$OUT/flex-fuzz
        "$gen" "$seed" "$base.plm" "$base.expected"
        echo "rc=0" >>"$base.expected"
        if flex_build "$base.plm" "$base" && flex_run "flex/fuzz/$seed" "$base.cmd" /dev/null "$base.expected"; then
            ok
            continue
        fi
        cp "$base.plm" "$OUT/flex-fuzz-fail-$seed.plm"
        echo "  program kept as $OUT/flex-fuzz-fail-$seed.plm"
        break
    done
}

# The run tests as commands on real FLEX: Compusense FLEX for the Dragon 64
# ($FLEX_DISK), booted from DragonDOS under XRoar.  The programs are put on
# a copy of the disk with tools/flexdisk.py, and the STARTUP file EXECs a
# command file that runs each one as "P,Tnn" (output to the printer, which
# XRoar appends to a file), after "P,MARK" (a separator line), then DONE
# (tests/flex/done.asm at $C600, where XRoar traps and quits).  The copy's
# boot image (the FLEX that DragonDOS loads, at track 1 sector 1) is
# patched not to ask for the date: XRoar's -type only reaches BASIC, not
# FLEX's keyboard driver.  Tests with PLMCARGS or an input file are skipped
# (FLEX's I command resets P's printer output), and FLEX has no exit status.
suite_dragonflex() {
    local disk=${FLEX_DISK:-$HOME/coco/flex/FLEX_1.3.2.VDK}
    local img=$OUT/dragonflex.vdk lp=$OUT/dragonflex.lp list=$OUT/dragonflex.exec
    local fd=(python3 -I "$ROOT/tools/flexdisk.py" "$img") f name base n=0 names=()
    command -v xroar >/dev/null || { echo "dragonflex: xroar not found (skipped)"; exit 0; }
    [ -r "$disk" ] || { echo "dragonflex: no Dragon FLEX disk image at $disk (set FLEX_DISK; skipped)"; exit 0; }
    cp "$disk" "$img"
    # FLEX's cold start calls its date prompt at $CA02 (JSR $CAAC): skip it
    if ! "${fd[@]}" patch @1/1 CA02 BDCAAC 121212; then
        bad dragonflex "this FLEX boot image can't be patched (Compusense FLEX 1.2/1.3 expected)"; return
    fi
    for prog in done mark; do
        if ! lwasm -9 -f srec -o "$OUT/dragonflex-$prog.s19" "$ROOT/tests/flex/$prog.asm" 2>"$OUT/dragonflex-$prog.err" ||
           ! "$BUILD/srec2flex" "$OUT/dragonflex-$prog.s19" "$OUT/dragonflex-$prog.cmd" ||
           ! "${fd[@]}" put "$OUT/dragonflex-$prog.cmd" "${prog^^}.CMD"; then
            bad dragonflex "cannot build tests/flex/$prog.asm"; return
        fi
    done
    : >"$list"
    for f in "$ROOT"/tests/run/*.plm; do
        grep -q '^/\* PLMCARGS:' "$f" && continue
        [ -e "${f%.plm}.in" ] && continue
        name=$(basename "$f" .plm)
        base=$OUT/dragonflex-$name
        n=$((n + 1))
        flex_build "$f" "$base" || { bad "dragonflex/$name" "build failed"; continue; }
        "${fd[@]}" put "$base.cmd" "$(printf 'T%02d.CMD' $n)" || { bad "dragonflex/$name" "no room on the disk"; continue; }
        printf 'P,MARK\nP,T%02d\n' $n >>"$list"
        names+=("$name")
    done
    printf 'DONE\n' >>"$list"
    printf 'EXEC TESTS\n' >"$OUT/dragonflex.startup"
    "${fd[@]}" puttext "$list" TESTS.TXT && "${fd[@]}" puttext "$OUT/dragonflex.startup" STARTUP.TXT ||
        { bad dragonflex "cannot write the command files"; return; }

    # RETURN dismisses the Dragon 64's PRESS ANY KEY; -timeout (emulated
    # seconds) only guards against a hang
    rm -f "$lp"
    timeout 900 xroar -q -machine dragon64 -ui null -ao null -no-ratelimit \
        -load-fd0 "$img" -type '\rBOOT\r' -lp-file "$lp" \
        -trap pc=0xC600 -trap-timeout 1 -timeout $((60 + 30 * n)) >/dev/null 2>&1
    # the printer output, split at the MARK lines; NUL (TTYSET's padding)
    # and LF dropped, CR made a newline
    python3 -I - "$lp" "$OUT/dragonflex-" "${names[@]}" <<'PY'
import sys
data = open(sys.argv[1], 'rb').read() if __import__('os').path.exists(sys.argv[1]) else b''
text = data.replace(b'\0', b'').replace(b'\n', b'').replace(b'\r', b'\n').decode('latin-1')
parts = text.split('=== plm09 ===\n')[1:]
for i, name in enumerate(sys.argv[3:]):
    with open(sys.argv[2] + name + '.out', 'w') as f:
        f.write(parts[i] if i < len(parts) else '')
PY
    for name in "${names[@]}"; do
        check_diff "dragonflex/$name" "$OUT/dragonflex-$name.out" "$ROOT/tests/run/$name.out" && ok
    done
}

# The run tests as OS-9 modules on real NitrOS-9: all of them are put in
# CMDS on a copy of a bootable NitrOS-9 Level 2 disk ($NITROS9_DISK), whose
# startup script runs each one with output redirected to a file; XRoar boots
# it on an emulated CoCo 3 ($COCO3_ROM is the Super Extended BASIC ROM), and
# the output files are copied back off the disk and compared with .out.
# Exit statuses are not visible this way and are not checked.  Skipped when
# xroar, toolshed's os9 tool, the disk image or the ROM are missing.
suite_nitros9() {
    local disk=${NITROS9_DISK:-$HOME/coco/nitros9.os9}
    local rom=${COCO3_ROM:-/usr/share/mame/roms/coco3.rom}
    local img=$OUT/nitros9.os9 script=$OUT/nitros9-startup f name base lname n=0
    for t in xroar os9; do
        command -v $t >/dev/null || { echo "nitros9: $t not found (skipped)"; exit 0; }
    done
    [ -r "$disk" ] || { echo "nitros9: no NitrOS-9 disk image at $disk (set NITROS9_DISK; skipped)"; exit 0; }
    [ -r "$rom" ] || { echo "nitros9: no CoCo 3 ROM at $rom (set COCO3_ROM; skipped)"; exit 0; }

    cp "$disk" "$img"
    # Images are often stored truncated after the last used sector, and XRoar
    # then sees fewer cylinders than the file system has (sectors written
    # beyond the end are lost).  Pad to DD.TOT (LSN0 bytes 0-2) sectors.
    local want
    want=$(od -An -tu1 -N3 "$img" | awk '{ print ($1 * 65536 + $2 * 256 + $3) * 256 }')
    [ "$want" -gt "$(stat -c %s "$img")" ] && truncate -s "$want" "$img"
    : >"$OUT/nitros9-empty"
    os9 copy -r "$OUT/nitros9-empty" "$img,empty.in" >/dev/null
    # -x: keep going when a command fails (exit.plm exits with status 42)
    printf -- '-x\r' >"$script"
    for f in "$ROOT"/tests/run/*.plm; do
        grep -q '^/\* PLMCARGS:' "$f" && continue
        name=$(basename "$f" .plm)
        # module and file both named t<name>: a module named after an
        # existing command (ECHO) would run that command instead
        lname=t$name
        base=$OUT/nitros9-$name
        os9_build "$f" "$base" "$lname" || { bad "nitros9/$name" "build failed"; continue; }
        os9 copy -r "$base.mod" "$img,CMDS/$lname" >/dev/null &&
            os9 attr -e -pe "$img,CMDS/$lname" >/dev/null || { bad "nitros9/$name" "os9 copy failed"; continue; }
        if [ -e "${f%.plm}.in" ]; then
            os9 copy -r -l "${f%.plm}.in" "$img,$name.in" >/dev/null
            printf '%s </dd/%s.in >/dd/%s.out\r' "$lname" "$name" "$name" >>"$script"
        else
            printf '%s </dd/empty.in >/dd/%s.out\r' "$lname" "$name" >>"$script"
        fi
        n=$((n + 1))
    done
    # last: tdone reads $FF6A, which XRoar traps on to quit
    if ! lwasm -9 -f os9 -o "$OUT/nitros9-done.mod" "$ROOT/tests/nitros9/done.asm" 2>"$OUT/nitros9-done.err" ||
       ! os9 copy -r "$OUT/nitros9-done.mod" "$img,CMDS/tdone" >/dev/null ||
       ! os9 attr -e -pe "$img,CMDS/tdone" >/dev/null; then
        bad nitros9 "cannot build tests/nitros9/done.asm"; return
    fi
    printf 'echo done >/dd/done.txt\rtdone\r' >>"$script"
    os9 copy -r -l "$script" "$img,startup" >/dev/null

    # -timeout (emulated seconds, about 20x real time here) only guards
    # against a hang; normally the trap on tdone's read ends the run
    timeout 900 xroar -q -machine coco3 -extbas "$rom" -ui null -ao null -no-ratelimit \
        -load-fd0 "$img" -type 'DOS\r' -trap read=0xFF6A -trap-timeout 1 \
        -timeout $((600 + 150 * n)) >/dev/null 2>&1
    if ! os9 dir "$img" 2>/dev/null | grep -qw done.txt; then
        bad nitros9 "startup script did not finish (see $img)"; return
    fi
    for f in "$ROOT"/tests/run/*.plm; do
        grep -q '^/\* PLMCARGS:' "$f" && continue
        name=$(basename "$f" .plm)
        base=$OUT/nitros9-$name
        rm -f "$base.out"
        os9 copy -l "$img,$name.out" "$base.out" >/dev/null 2>&1 || : >"$base.out"
        check_diff "nitros9/$name" "$base.out" "${f%.plm}.out" && ok
    done
}

suite_fuzz() {
    local n=${1:-200} gen=$BUILD/exprgen seed act
    if [ ! -e "$ROOT/tests/gen/exprgen.c" ]; then
        echo "fuzz: no generator yet (skipped)"; exit 0
    fi
    cc -std=c11 -O2 -Wall -Wextra -Werror -o "$gen" "$ROOT/tests/gen/exprgen.c" || { bad fuzz "generator build"; return; }
    for seed in $(seq 1 "$n"); do
        "$gen" "$seed" "$OUT/fuzz.plm" "$OUT/fuzz.out"
        act=$OUT/fuzz.stdout
        echo "rc=0" >>"$OUT/fuzz.out"
        if ! build_and_run "$OUT/fuzz.plm" "$act"; then
            bad "fuzz/$seed" "build failed"
        elif check_diff "fuzz/$seed" "$act" "$OUT/fuzz.out"; then
            ok
            continue
        fi
        cp "$OUT/fuzz.plm" "$OUT/fuzz-fail-$seed.plm"
        echo "  program kept as $OUT/fuzz-fail-$seed.plm"
        break
    done
}

case ${1:-} in
    sim)   suite_sim ;;
    lex)   suite_golden lex --tokens tok ;;
    parse) suite_golden parse --ast ast ;;
    err)   suite_err ;;
    run)   suite_run ;;
    fuzz)  suite_fuzz "${2:-200}" ;;
    coco)  suite_coco ;;
    os9)   suite_os9 "${2:-25}" ;;
    nitros9) suite_nitros9 ;;
    flex)  suite_flex "${2:-25}" ;;
    dragonflex) suite_dragonflex ;;
    xroar) suite_xroar ;;
    one)
        act=$OUT/one.stdout
        build_and_run "$2" "$act" /dev/stdin || exit 1
        cat "$act"; exit 0 ;;
    *) echo "usage: $0 sim|lex|parse|err|run|coco|xroar|os9 [N]|nitros9|fuzz [N]|one FILE" >&2; exit 2 ;;
esac

if [ $((pass + fail)) -eq 0 ] && [ "$1" != fuzz ]; then
    echo "$1: no tests"
else
    echo "$1: $pass passed, $fail failed"
fi
[ "$fail" -eq 0 ]
