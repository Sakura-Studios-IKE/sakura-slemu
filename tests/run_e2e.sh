#!/bin/sh
# run_e2e.sh — end-to-end pipeline test.
#
# Each script under tests/scripts/*.lsl is compiled with lslc (path in $LSLC
# or assumed at ../sakura-lslc/lslc), then run in slemu and checked against
# tests/expect/<name>.txt (substring match per line, in order).
set -u
SLEMU="${1:-./slemu}"
LSLC="${LSLC:-../sakura-lslc/lslc}"
HERE="$(dirname "$0")"

if [ ! -x "$LSLC" ]; then
    echo "skip: lslc not found at $LSLC"; exit 0
fi

pass=0; fail=0
for src in "$HERE"/scripts/*.lsl; do
    [ -e "$src" ] || continue
    base="$(basename "$src" .lsl)"
    expect="$HERE/expect/$base.txt"
    [ -f "$expect" ] || continue
    bc="${src%.lsl}.lslbc"
    "$LSLC" -c "$src" >/dev/null 2>&1 || { echo "  FAIL $base (lslc)"; fail=$((fail+1)); continue; }

    vol="/tmp/slemu_test_$base"
    rm -rf "$vol"
    extra=""
    case "$base" in
        04_*) extra="--http-fixture $HERE/scripts/04_fixture.txt" ;;
    esac
    out=$("$SLEMU" --steps 100 --timeout 3 --volume "$vol" --owner-balance 100 $extra "$bc" 2>&1)

    ok=1
    while IFS= read -r line; do
        case "$out" in
            *"$line"*) ;;
            *) ok=0; break ;;
        esac
    done < "$expect"

    if [ $ok -eq 1 ]; then
        printf "  PASS  %s\n" "$base"; pass=$((pass+1))
    else
        printf "  FAIL  %s\n" "$base"
        echo "------- expected (each line a substring): -------"
        cat "$expect" | sed 's/^/    /'
        echo "------- got: -------"
        echo "$out" | sed 's/^/    /'
        fail=$((fail+1))
    fi
done

echo
echo "Result: $pass passing / $((pass+fail)) e2e tests"
[ $fail -eq 0 ]
