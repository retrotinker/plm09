#!/usr/bin/env bash
# plm09 test runner.
#   run_tests.sh sim|lex|parse|err|run     run one suite
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
    "${LWASM[@]}" -l"${2%.s19}.lst" -o "$2" "$1" 2>"$2.err"
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

suite_sim() {
    local f name s19
    for f in "$ROOT"/sim/tests/*.asm; do
        [ -e "$f" ] || continue
        name=sim/$(basename "$f" .asm)
        s19=$OUT/$(basename "$f" .asm).s19
        if ! assemble "$f" "$s19"; then bad "$name" "lwasm failed"; cat "$s19.err"; continue; fi
        "$SIM" "$s19" </dev/null >"$s19.stdout" 2>"$s19.stderr"
        echo "rc=$?" >>"$s19.stdout"
        check_diff "$name" "$s19.stdout" "${f%.asm}.out" && ok
    done
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

# compile+assemble+run; stdout plus "rc=N" goes to $2
build_and_run() {
    local base=${2%.stdout}
    "$PLMC" -o "$base.asm" "$1" 2>"$base.plmc.err" || { cat "$base.plmc.err"; return 10; }
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
        build_and_run "$OUT/fuzz.plm" "$act" || { bad "fuzz/$seed" "build failed"; break; }
        echo "rc=0" >>"$OUT/fuzz.out"
        check_diff "fuzz/$seed" "$act" "$OUT/fuzz.out" && ok || break
    done
}

case ${1:-} in
    sim)   suite_sim ;;
    lex)   suite_golden lex --tokens tok ;;
    parse) suite_golden parse --ast ast ;;
    err)   suite_err ;;
    run)   suite_run ;;
    fuzz)  suite_fuzz "${2:-200}" ;;
    one)
        act=$OUT/one.stdout
        build_and_run "$2" "$act" /dev/stdin || exit 1
        cat "$act"; exit 0 ;;
    *) echo "usage: $0 sim|lex|parse|err|run|fuzz [N]|one FILE" >&2; exit 2 ;;
esac

if [ $((pass + fail)) -eq 0 ] && [ "$1" != fuzz ]; then
    echo "$1: no tests"
else
    echo "$1: $pass passed, $fail failed"
fi
[ "$fail" -eq 0 ]
