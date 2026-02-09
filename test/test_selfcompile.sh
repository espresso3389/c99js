#!/bin/bash
# Self-compilation test for c99js
# Verifies that the compiler can compile itself and produce identical output.
#
# Steps:
#   1. Build c99js native binary (zig build or clang)
#   2. Compile selfcompile.c → selfcompile.js  (native exe → JS)
#   3. Compile selfcompile.c → selfcompile2.js (JS compiler → JS)
#   4. Verify selfcompile.js and selfcompile2.js are identical
#   5. Run primitive tests with selfcompile2.js
#
# Usage: ./test/test_selfcompile.sh [node-path]

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Auto-detect node/bun
if [ -n "${1:-}" ]; then
    NODE="$1"
elif command -v node >/dev/null 2>&1; then
    NODE="node"
elif command -v bun >/dev/null 2>&1; then
    NODE="bun"
else
    echo "Error: node or bun not found."
    exit 1
fi

cd "$PROJECT_DIR"

PASS=0
FAIL=0

step() {
    printf "\n=== %s ===\n" "$1"
}

check() {
    local label="$1"
    local rc="$2"
    printf "  %-45s " "$label"
    if [ "$rc" -eq 0 ]; then
        echo "PASS"
        PASS=$((PASS + 1))
    else
        echo "FAIL"
        FAIL=$((FAIL + 1))
    fi
}

# --- Step 1: Build native compiler ---
step "Step 1: Build native compiler"

EXE=""
if command -v zig >/dev/null 2>&1; then
    echo "  Using: zig build"
    zig build -Doptimize=ReleaseFast 2>&1
    rc=$?
    check "zig build" $rc
    if [ $rc -ne 0 ]; then echo "Cannot continue without compiler."; exit 1; fi
    EXE="./zig-out/bin/c99js"
    # On Windows, zig produces .exe
    [ -f "${EXE}.exe" ] && EXE="${EXE}.exe"
elif command -v clang >/dev/null 2>&1; then
    echo "  Using: clang"
    clang -std=c99 -O2 -D_CRT_SECURE_NO_WARNINGS -o c99js \
        src/util.c src/type.c src/lexer.c src/ast.c src/symtab.c \
        src/preprocess.c src/parser.c src/sema.c src/codegen.c src/main.c 2>&1
    rc=$?
    check "clang build" $rc
    if [ $rc -ne 0 ]; then echo "Cannot continue without compiler."; exit 1; fi
    EXE="./c99js"
    [ -f "c99js.exe" ] && EXE="./c99js.exe"
elif command -v gcc >/dev/null 2>&1; then
    echo "  Using: gcc"
    gcc -std=c99 -O2 -D_CRT_SECURE_NO_WARNINGS -o c99js \
        src/util.c src/type.c src/lexer.c src/ast.c src/symtab.c \
        src/preprocess.c src/parser.c src/sema.c src/codegen.c src/main.c 2>&1
    rc=$?
    check "gcc build" $rc
    if [ $rc -ne 0 ]; then echo "Cannot continue without compiler."; exit 1; fi
    EXE="./c99js"
else
    echo "Error: No C compiler found (zig, clang, or gcc)."
    exit 1
fi

echo "  Binary: $EXE"

# --- Step 2: Compile selfcompile.c with native compiler (C → JS) ---
step "Step 2: Native compiler compiles itself (C -> JS)"

$EXE selfcompile.c -o selfcompile.js 2>&1
check "c99js selfcompile.c -> selfcompile.js" $?

# Quick smoke test: test_tiny.c returns 42
"$NODE" selfcompile.js test/test_tiny.c -o _smoke.js 2>&1
"$NODE" _smoke.js 2>&1
rc=$?
if [ "$rc" -eq 42 ]; then rc=0; else rc=1; fi
check "selfcompile.js smoke test (test_tiny -> exit 42)" $rc
rm -f _smoke.js

# --- Step 3: Compile selfcompile.c with JS compiler (JS → JS) ---
step "Step 3: JS compiler compiles itself (JS -> JS)"

"$NODE" selfcompile.js selfcompile.c -o selfcompile2.js 2>&1
check "selfcompile.js selfcompile.c -> selfcompile2.js" $?

# --- Step 4: Verify outputs are identical ---
step "Step 4: Compare selfcompile.js and selfcompile2.js"

diff <(tr -d '\r' < selfcompile.js) <(tr -d '\r' < selfcompile2.js) >/dev/null 2>&1
check "selfcompile.js == selfcompile2.js (byte-identical)" $?

# --- Step 5: Run primitive tests with self-compiled compiler ---
step "Step 5: Primitive tests with self-compiled compiler"

bash test/run_tests.sh "$NODE selfcompile2.js" "$NODE" 2>&1
check "All primitive tests pass with selfcompile2.js" $?

# --- Cleanup ---
rm -f selfcompile.js selfcompile2.js _test_tmp.js

# --- Summary ---
echo ""
echo "========================================"
echo "Self-compilation: $PASS passed, $FAIL failed"
echo "========================================"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
