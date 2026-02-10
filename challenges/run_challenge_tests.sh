#!/usr/bin/env bash
# Run all challenge tests under challenges/tests/*/*.c.
#
# Each test is compiled with c99js to a temporary JS file and then executed
# with node or bun. If an `expected.txt` exists next to the test source, the
# output must match exactly (ignoring CRLF differences). Otherwise exit code
# must be 0.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

C99JS="${1:-$PROJECT_DIR/c99js.exe}"

# Auto-detect JS runtime (node/bun) unless passed explicitly.
if [ -n "${2:-}" ]; then
  JSRT="$2"
elif command -v node >/dev/null 2>&1; then
  JSRT="node"
elif command -v bun >/dev/null 2>&1; then
  JSRT="bun"
else
  echo "Error: node/bun not found. Pass it as the 2nd argument."
  exit 1
fi

PASS=0
FAIL=0
SKIP=0

cleanup_tmpdir() {
  if [ -n "${TMPDIRS:-}" ]; then
    # shellcheck disable=SC2086
    rm -rf $TMPDIRS
  fi
}
trap cleanup_tmpdir EXIT

echo "c99js challenge test suite"
echo "  compiler: $C99JS"
echo "  jsrt:     $("$JSRT" --version 2>/dev/null || echo "$JSRT")"
echo ""

cd "$PROJECT_DIR"

TEST_ROOT="$PROJECT_DIR/challenges/tests"

if [ ! -d "$TEST_ROOT" ]; then
  echo "Error: missing $TEST_ROOT"
  exit 1
fi

run_one() {
  local src="$1"
  local test_dir suite suite_dir expected exit_expect_file expect_exit
  local tmpdir tmpjs compile_log run_log ec

  test_dir="$(cd "$(dirname "$src")" && pwd)"
  suite="$(basename "$test_dir")"
  suite_dir="$PROJECT_DIR/challenges/$suite"
  expected="$test_dir/expected.txt"
  exit_expect_file="$test_dir/$(basename "$src" .c).exit"
  expect_exit=0
  if [ -f "$exit_expect_file" ]; then
    expect_exit="$(tr -d '\r' < "$exit_expect_file" | tr -d '[:space:]')"
    if ! printf '%s' "$expect_exit" | grep -Eq '^[0-9]+$'; then
      echo "FAIL (bad exit spec)"
      echo "  $exit_expect_file must contain an integer exit code"
      FAIL=$((FAIL + 1))
      return
    fi
  fi

  printf "  %-28s " "${suite}/$(basename "$src")"

  tmpdir="$(mktemp -d 2>/dev/null || mktemp -d -t c99js_challenge)"
  TMPDIRS="${TMPDIRS:-} $tmpdir"
  tmpjs="$tmpdir/out.js"

  # Compiled output uses a relative require("./runtime/runtime.js"). Run each
  # test in an isolated temp dir but copy the runtime in so that require works.
  mkdir -p "$tmpdir/runtime"
  cp "$PROJECT_DIR/runtime/runtime.js" "$tmpdir/runtime/runtime.js"

  # Copy any non-C helper inputs living next to the test (e.g. .lisp files).
  # This keeps tests hermetic while still allowing fopen("test_input.lisp") etc.
  while IFS= read -r -d '' asset; do
    cp "$asset" "$tmpdir/"
  done < <(
    find "$test_dir" -maxdepth 1 -type f \
      ! -name "*.c" \
      ! -name "expected.txt" \
      ! -name "expected_exit.txt" \
      -print0 2>/dev/null
  )

  # Include paths:
  # - test_dir: for patched/inlined sources living next to the driver
  # - suite_dir (+ common subdirs): for project headers/sources
  # Note: even for single-file tests, these are harmless.
  local -a inc
  inc=("-I" "$test_dir")
  if [ -d "$suite_dir" ]; then
    inc+=("-I" "$suite_dir")
    [ -d "$suite_dir/include" ] && inc+=("-I" "$suite_dir/include")
    [ -d "$suite_dir/src" ] && inc+=("-I" "$suite_dir/src")
  fi

  compile_log="$("$C99JS" "${inc[@]}" "$src" -o "$tmpjs" 2>&1)" || {
    echo "FAIL (compile)"
    echo "$compile_log" | sed -n '1,80p'
    FAIL=$((FAIL + 1))
    return
  }

  run_log="$(cd "$tmpdir" && "$JSRT" "$tmpjs" 2>&1)"
  ec=$?

  if [ "$ec" -ne "$expect_exit" ]; then
    echo "FAIL (exit $ec, want $expect_exit)"
    echo "$run_log" | sed -n '1,80p'
    FAIL=$((FAIL + 1))
    return
  fi

  if [ -f "$expected" ]; then
    # Normalize CRLF on both sides.
    local expect_out actual_out
    expect_out="$(tr -d '\r' < "$expected")"
    actual_out="$(printf '%s' "$run_log" | tr -d '\r')"
    if [ "$actual_out" != "$expect_out" ]; then
      echo "FAIL (output mismatch)"
      echo "    expected: $(head -1 "$expected" 2>/dev/null)..."
      echo "    got:      $(printf '%s' "$run_log" | head -1)..."
      FAIL=$((FAIL + 1))
      return
    fi
  fi

  echo "PASS"
  PASS=$((PASS + 1))
}

# Collect *.c drivers that define main().
TESTS=$(
  find "$TEST_ROOT" -mindepth 2 -maxdepth 2 -type f -name "*.c" -print 2>/dev/null | sort
)

if [ -z "$TESTS" ]; then
  echo "No tests found under $TEST_ROOT"
  exit 1
fi

while IFS= read -r f; do
  # Skip files that are not meant to be compiled directly (debug dumps, patched units, etc).
  base="$(basename "$f")"
  if [ "$base" = "c99js_pp.c" ]; then
    SKIP=$((SKIP + 1))
    continue
  fi

  if grep -Eq '^[[:space:]]*(static[[:space:]]+)?int[[:space:]]+main[[:space:]]*\(' "$f"; then
    run_one "$f"
  else
    SKIP=$((SKIP + 1))
  fi
done <<<"$TESTS"

echo ""
echo "Results: $PASS passed, $FAIL failed, $SKIP skipped (no main) (total $((PASS + FAIL + SKIP)))"

if [ "$FAIL" -gt 0 ]; then
  exit 1
fi
