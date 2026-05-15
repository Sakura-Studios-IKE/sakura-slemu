#!/bin/sh
# run_coverage.sh — exhaustive runtime-coverage suite for slemu.
#
# Compiles every .lsl under tests/coverage/ (skipping the *_b.lsl partners
# of multi-script scenarios), then for each scenario runs slemu and asserts
# that every line in tests/coverage/expect/<name>.txt appears (as a
# substring) in the JSON-events stdout, in order.
#
# Naming conventions:
#
#   NN_name.lsl          single-script scenario
#   NN_name_a.lsl + _b.lsl  paired multi-script scenario (root + child)
#                          driver lives at NN_name.cmds / expect file at
#                          NN_name.txt — i.e. the base name is the prefix
#                          without _a/_b.
#   NN_name.fixture      optional --http-fixture file for this scenario
#
# Special-case scenarios:
#
#   23_lsd_persist  is run twice with the SAME volume to exercise persistence
#                   across emulator restarts.
#
set -u

SLEMU="${1:-./slemu}"
LSLC="${LSLC:-../sakura-lslc/lslc}"
HERE="$(cd "$(dirname "$0")" && pwd)"

[ -x "$SLEMU" ] || { echo "slemu binary not found: $SLEMU" >&2; exit 2; }
[ -x "$LSLC"  ] || { echo "lslc binary not found: $LSLC" >&2; exit 2; }

# ---- compile every .lsl in the coverage directory ----
fail_compile=0
for src in "$HERE"/*.lsl; do
    [ -e "$src" ] || continue
    if ! "$LSLC" -c "$src" >/dev/null 2>&1; then
        printf '  COMPILE-FAIL %s\n' "$(basename "$src")"
        fail_compile=$((fail_compile+1))
    fi
done
if [ "$fail_compile" -gt 0 ]; then
    echo "Compilation failures: $fail_compile — aborting."
    exit 1
fi

# ---- enumerate scenarios ----
# A scenario is identified by a base name that has a matching expect file.
# Multi-script scenarios have <base>_a.lsl + <base>_b.lsl partners; single
# scripts have <base>.lsl.
scenarios=""
for ex in "$HERE"/expect/*.txt; do
    [ -e "$ex" ] || continue
    base="$(basename "$ex" .txt)"
    scenarios="$scenarios $base"
done

pass=0; fail=0
fail_names=""

verify_output() {
    out="$1"; expect_file="$2"
    while IFS= read -r line; do
        # blank/empty expect lines are skipped
        [ -n "$line" ] || continue
        case "$out" in
            *"$line"*) ;;
            *) return 1 ;;
        esac
    done < "$expect_file"
    return 0
}

for base in $scenarios; do
    expect="$HERE/expect/$base.txt"
    cmds="$HERE/$base.cmds"
    fixture="$HERE/$base.fixture"

    # Locate script(s): pair scripts if _a / _b exist
    src_a="$HERE/${base}_a.lsl"; bc_a="${src_a%.lsl}.lslbc"
    src_b="$HERE/${base}_b.lsl"; bc_b="${src_b%.lsl}.lslbc"
    if [ -f "$src_a" ] && [ -f "$src_b" ]; then
        scripts="$bc_a $bc_b"
    elif [ -f "$HERE/$base.lsl" ]; then
        scripts="$HERE/$base.lslbc"
    else
        echo "  FAIL  $base  (no .lsl)"; fail=$((fail+1))
        fail_names="$fail_names $base"; continue
    fi

    [ -f "$cmds" ] || cmds=""
    [ -f "$fixture" ] || fixture=""

    vol="/tmp/slemu_cov_$base"
    rm -rf "$vol"

    extra_args=""
    [ -n "$cmds" ]    && extra_args="$extra_args --commands $cmds"
    [ -n "$fixture" ] && extra_args="$extra_args --http-fixture $fixture"

    case "$base" in
      23_lsd_persist)
        # Two-pass: first pass writes, second pass reads from same volume.
        out1=$("$SLEMU" --json-events --steps 500 --timeout 5 \
                       --volume "$vol" $extra_args --owner-balance 100 \
                       $scripts 2>&1)
        out=$("$SLEMU" --json-events --steps 500 --timeout 5 \
                       --volume "$vol" $extra_args --owner-balance 100 \
                       $scripts 2>&1)
        ;;
      *)
        out=$("$SLEMU" --json-events --steps 500 --timeout 5 \
                       --volume "$vol" $extra_args --owner-balance 100 \
                       $scripts 2>&1)
        ;;
    esac

    if verify_output "$out" "$expect"; then
        printf '  PASS  %s\n' "$base"
        pass=$((pass+1))
    else
        printf '  FAIL  %s\n' "$base"
        echo "------- expected (each line a substring, in order): -------"
        sed 's/^/    /' "$expect"
        echo "------- got: -------"
        printf '%s\n' "$out" | sed 's/^/    /'
        fail=$((fail+1))
        fail_names="$fail_names $base"
    fi
done

total=$((pass+fail))
echo
echo "=========================================================="
echo "Coverage result: $pass / $total scenarios passing"
if [ "$fail" -gt 0 ]; then
    echo "Failed:"
    for n in $fail_names; do echo "    $n"; done
fi
echo "=========================================================="

[ "$fail" -eq 0 ]
