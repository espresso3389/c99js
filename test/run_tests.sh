#!/bin/bash
# Test runner for c99js compiler
# Usage: ./test/run_tests.sh [path-to-c99js] [path-to-node]
#
# Examples:
#   ./test/run_tests.sh ./c99js.exe node          # native compiler
#   ./test/run_tests.sh "node selfcompile.js"      # self-compiled compiler

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

C99JS="${1:-$PROJECT_DIR/c99js.exe}"

# Auto-detect node
if [ -n "${2:-}" ]; then
    NODE="$2"
elif command -v node >/dev/null 2>&1; then
    NODE="node"
elif [ -x "$(command -v bun 2>/dev/null)" ]; then
    NODE="bun"
else
    # Try common locations on Windows
    for p in \
        "$USERPROFILE/scoop/apps/nodejs/current/node.exe" \
        "$PROGRAMFILES/nodejs/node.exe" \
        "/c/Program Files/nodejs/node.exe"; do
        if [ -x "$p" ]; then NODE="$p"; break; fi
    done
fi

if [ -z "${NODE:-}" ]; then
    echo "Error: node not found. Install Node.js or pass path as second argument."
    exit 1
fi

PASS=0
FAIL=0
SKIP=0
TMPJS="$PROJECT_DIR/_test_tmp.js"

cleanup() { rm -f "$TMPJS"; }
trap cleanup EXIT

run_test() {
    local src="$1"
    local expect_exit="$2"
    local expect_file="$3"  # empty string if no stdout expected
    local name
    name=$(basename "$src" .c)

    printf "  %-25s " "$name"

    # Compile
    if ! $C99JS "$src" -o "$TMPJS" >/dev/null 2>&1; then
        echo "FAIL (compile error)"
        FAIL=$((FAIL + 1))
        return
    fi

    # Run and capture output + exit code
    local actual_out actual_exit
    actual_out=$("$NODE" "$TMPJS" 2>&1) && actual_exit=$? || actual_exit=$?

    # Check exit code
    if [ "$actual_exit" -ne "$expect_exit" ]; then
        echo "FAIL (exit: expected $expect_exit, got $actual_exit)"
        FAIL=$((FAIL + 1))
        return
    fi

    # Check stdout if expected
    if [ -n "$expect_file" ] && [ -f "$expect_file" ]; then
        local expect_out
        expect_out=$(tr -d '\r' < "$expect_file")
        actual_out=$(printf '%s' "$actual_out" | tr -d '\r')
        if [ "$actual_out" != "$expect_out" ]; then
            echo "FAIL (output mismatch)"
            echo "    expected: $(head -1 "$expect_file")..."
            echo "    got:      $(printf '%s' "$actual_out" | head -1)..."
            FAIL=$((FAIL + 1))
            return
        fi
    fi

    echo "PASS"
    PASS=$((PASS + 1))
}

echo "c99js test suite"
echo "  compiler: $C99JS"
echo "  node:     $("$NODE" --version 2>/dev/null || echo "$NODE")"
echo ""

cd "$PROJECT_DIR"

# Tests with expected exit code and optional expected output
# Format: run_test <source> <expected_exit> <expected_output_file>
run_test test/test_tiny.c           42 ""
run_test test/test_min.c             0 ""
run_test test/test_str.c             0 ""
run_test test/test_tilde.c          48 ""
run_test test/test_struct_basic.c    5 ""
run_test test/test_struct_sizeof.c   8 ""
run_test test/test_flex.c            0 ""
run_test test/test_array_member.c    0 ""
run_test test/test_ginit.c           1 ""
run_test test/test_ginit2.c         10 ""
run_test test/test_ginit3.c         10 ""
run_test test/test_ginit4.c          2 ""
run_test test/test_anon.c            0 "test/expected/test_anon.txt"
run_test test/test_global_init.c     0 "test/expected/test_global_init.txt"
run_test test/test_basic.c           0 "test/expected/test_basic.txt"
run_test test/test_string.c          0 "test/expected/test_string.txt"
run_test test/test_struct.c          0 "test/expected/test_struct.txt"
run_test test/test_funcptr.c         0 "test/expected/test_funcptr.txt"

echo ""
echo "Results: $PASS passed, $FAIL failed, $SKIP skipped (total $((PASS + FAIL + SKIP)))"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
