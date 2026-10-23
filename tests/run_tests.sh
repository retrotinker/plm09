#!/usr/bin/env bash
# plm09 test runner.
#   run_tests.sh sim|lex|parse|err|run|coco|xroar   run one suite
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
# "; SIMARGS: ..." passes extra simulator options.  Files whose listing has
# LEN_BEGIN/LEN_END also get the trace-vs-listing instruction length check.
sim_one() {    # $1 = asm, $2 = expected .out
    local name s19 lst args b e
    name=sim/$(basename "$1" .asm)
    s19=$OUT/$(basename "$1" .asm).s19
    lst=${s19%.s19}.lst
    if ! assemble "$1" "$s19"; then bad "$name" "lwasm failed"; cat "$s19.err"; return; fi
    args=$(sed -n '1s/^; SIMARGS: *//p' "$1")
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
    local f name act want
    for f in "$ROOT"/tests/err/*.plm; do
        [ -e "$f" ] || continue
        name=err/$(basename "$f" .plm)
        act=$OUT/err-$(basename "$f" .plm).stderr
        if "$PLMC" -o "$OUT/err.asm" "$f" 2>"$act" >/dev/null; then
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
        [ -e "$f" ] || continue
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
    xroar) suite_xroar ;;
    one)
        act=$OUT/one.stdout
        build_and_run "$2" "$act" /dev/stdin || exit 1
        cat "$act"; exit 0 ;;
    *) echo "usage: $0 sim|lex|parse|err|run|coco|xroar|fuzz [N]|one FILE" >&2; exit 2 ;;
esac

if [ $((pass + fail)) -eq 0 ] && [ "$1" != fuzz ]; then
    echo "$1: no tests"
else
    echo "$1: $pass passed, $fail failed"
fi
[ "$fail" -eq 0 ]
