# c99js Showcases & Challenges

Real-world C projects compiled to JavaScript with c99js.

## Showcases

Large-scale projects that demonstrate c99js capabilities.

| Project | Lines | Domain | Status |
|---------|-------|--------|--------|
| [antirez/voxtral.c](https://github.com/antirez/voxtral.c) | ~5,000 | ML inference (speech-to-text) | Compiles and runs |
| c99js (self) | ~5,500 | Compiler | Compiles and self-verifies |

### voxtral.c -- Mistral AI Speech-to-Text Engine

**Author:** Salvatore Sanfilippo (creator of Redis)

voxtral.c is a pure C implementation of the Mistral AI Voxtral Realtime 4B
speech-to-text model. It implements a full neural network inference pipeline:
WAV input, mel spectrogram computation, convolutional encoder, transformer
decoder with KV cache, and BPE tokenizer -- all in C99.

The entire project compiles to a single JavaScript file and runs under
Node.js or Bun. The compiled program parses command-line arguments, prints
usage information, and can load model weights from safetensors files.

```
$ c99js -I compat -I . voxtral_unity.c -o voxtral.js
$ bun voxtral.js --help
voxtral.c --- Voxtral Realtime 4B speech-to-text

Usage: voxtral.js -d <model_dir> (-i <input.wav> | --stdin | --from-mic) [options]
...
```

voxtral.c uses POSIX-specific APIs (`mmap`, `open`, `fstat`, `sigaction`)
that are not available in JavaScript. A thin compatibility layer provides
these through standard C I/O:

- **`mmap`** is implemented as `malloc` + `fread`
- **`open`/`close`/`fstat`** are implemented via `fopen`/`fclose`/`ftell`
- **`sigaction`/`sigemptyset`** are provided as no-op stubs
- **`gettimeofday`** returns zeros (timing is cosmetic)
- **`cblas_sgemm`** is replaced with a triple-nested loop

C features exercised: structs with arrays of structs, `uint16_t` BF16/F16
to float conversion via `memcpy` bit manipulation, extensive `float` math,
`snprintf`/`fprintf`/`perror`/`fflush`, signal handling, variadic argument
parsing, large-scale matrix operations, ring buffers and state machines.

### c99js -- Self-Compilation (Bootstrapping)

The most significant test of c99js is compiling itself. The resulting
JavaScript compiler produces byte-identical output to the native compiler,
proving full correctness.

```
# Step 1: Build native compiler
clang -std=c99 -O2 -o c99js src/*.c

# Step 2: Native compiler -> JS compiler
./c99js selfcompile.c -o selfcompile.js

# Step 3: JS compiler compiles itself
node selfcompile.js selfcompile.c -o selfcompile2.js

# Step 4: Verify byte-identical output
diff selfcompile.js selfcompile2.js   # no output
```

C features exercised: recursive descent parser with a full AST, arena
allocator, hash tables, variadic functions (`va_start`/`va_arg`/`va_end`),
function pointers, preprocessor with macro expansion/`#if`
evaluation/stringification/token pasting, file I/O, `qsort`.

## Build Challenges

Pure C projects from GitHub, tested against the c99js compiler.

| # | Project | Lines | Status | Notes |
|---|---------|-------|--------|-------|
| 1 | [kokke/tiny-AES-c](https://github.com/kokke/tiny-AES-c) | ~570 | **PASS** | AES-128 ECB/CBC/CTR all correct |
| 2 | [CTrabant/teeny-sha1](https://github.com/CTrabant/teeny-sha1) | ~160 | **PASS** | All 4 SHA-1 vectors correct |
| 3 | [kokke/tiny-regex-c](https://github.com/kokke/tiny-regex-c) | ~520 | **PASS** | 26/26 regex tests passed |
| 4 | [Robert-van-Engelen/tinylisp](https://github.com/Robert-van-Engelen/tinylisp) | ~385 | **PASS** | GC version: arithmetic, lambda, recursion, fib(10), closures, let* |
| 5 | [codeplea/tinyexpr](https://github.com/codeplea/tinyexpr) | ~600 | **PASS** | All 8 math expression tests passed |
| 6 | [rxi/ini](https://github.com/rxi/ini) | ~200 | **PASS** | INI parse/read all tests passed |
| 7 | [rswier/c4](https://github.com/rswier/c4) | ~365 | **PASS** | C interpreter VM compiles and runs hello.c correctly |
| 8 | [Robert-van-Engelen/lisp](https://github.com/Robert-van-Engelen/lisp) | ~730 | **PASS** | Full interpreter works: NaN-boxing, setjmp/longjmp, lambda, define, cond |

**Score: 8/8 passing (compile+run)**

### 1. tiny-AES-c -- PASS

AES-128 encryption/decryption compiles and all test vectors pass:

```
ECB encrypt: SUCCESS!
ECB decrypt: SUCCESS!
CBC encrypt: SUCCESS!
CTR xcrypt:  SUCCESS!
```

Previously blocked by `#if defined(ECB)` preprocessor expressions.
Fixed by processing `defined()` operator before macro expansion (C99 6.10.1).

### 2. teeny-sha1 -- PASS

Compiled and ran with workarounds for uint64 bit-shifting (split into hi/lo 32-bit words)
and rotate-left masking (JS `>>` is signed). All 4 standard SHA-1 test vectors pass:

```
Test 1: SHA-1 of empty string       -- PASS
Test 2: SHA-1 of "abc"              -- PASS
Test 3: SHA-1 of long test vector   -- PASS
Test 4: SHA-1 of "The quick brown fox jumps over the lazy dog" -- PASS
```

### 3. tiny-regex-c -- PASS

Full regex engine compiles and passes all 26 test cases covering: literal patterns,
digit/word/whitespace classes (`\d`, `\w`, `\s`), character classes `[a-z]`,
anchors `^`/`$`, quantifiers `+`/`?`/`*`, dot `.`, and combined patterns.

```
=== Results: 26/26 tests passed ===
```

### 4. tinylisp -- PASS

Full NaN-boxing Lisp interpreter with REPL. Uses `double` type punning via `unsigned long long`
casts to encode tagged values (ATOM, PRIM, CONS, CLOS, NIL) in NaN payloads.

Uses `tinylisp-gc.c` (GC version with reference counting + mark-sweep, N=8192).
The original `tinylisp.c` (N=1024, no real GC) aborts on `(fib 10)` even in native C
due to insufficient cell pool for tree-recursive fibonacci.

Required multiple compiler fixes to work:
- BigInt representation for doubles (preserves NaN payloads that JS normally canonicalizes)
- `getchar()`/`putchar()`/`puts()` runtime support
- `sscanf` length modifier (`%lg`) and `%n` format support
- Signed `long long` (TY_LLONG) BigInt codegen
- Ternary expression type coercion for mixed `long long`/`double` branches
- Function pointer call return type derivation in sema
- `setjmp`/`longjmp` for GC version error handling

```
(+ 1 2) => 3
(* 4 5) => 20
(define square (lambda (x) (* x x)))
(square 7) => 49
(define fact (lambda (n) (if (< n 2) 1 (* n (fact (- n 1))))))
(fact 10) => 3628800
(define fib (lambda (n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))))
(fib 10) => 55
```

### 5. tinyexpr -- PASS

Full math expression parser/evaluator compiles and produces correct results for all tests:

```
Test 1: te_interp("2+3") = 5
Test 2: te_interp("sqrt(9)") = 3
Test 3: te_interp("sin(0)") = 0
Test 4: te_interp("2^10") = 1024
Test 5: te_interp("3*4+2") = 14
Test 6: te_interp("pi()") = 3.14159
Test 7: te_interp("fac(5)") = 120
Test 8: te_interp("pow(2,8)") = 256
```

### 6. rxi/ini -- PASS

INI file parser compiles and all tests pass: writing a test INI file, parsing it back,
and reading values by section/key. Null return for missing keys works correctly.

```
[owner] name = John
[owner] organization = Acme Inc
[database] server = 192.168.1.1
[database] port = 143
All tests passed!
```

### 7. c4 -- PASS

A C interpreter/VM written in C (~365 lines). Compiles C source code to bytecode and executes
it on a virtual stack machine. Uses `#define int long long` to make all `int` types 64-bit.

Required POSIX stubs and several BigInt-related compiler fixes:
- Stub headers for `<memory.h>`, `<unistd.h>`, `<fcntl.h>`
- POSIX `open()`/`read()`/`close()` runtime implementations
- BigInt→Number conversion in pre/post increment/decrement for `long long` types
- BigInt→Number conversion in pointer arithmetic (`ptr + BigInt_index`)
- BigInt→pointer cast handling (`(char *)long_long_value`)
- `process.exit(Number(main_result))` for BigInt return values

Successfully compiles and runs `hello.c`:
```
hello, world
exit(0) cycle = 9
```

### 8. Robert-van-Engelen/lisp -- PASS

Full NaN-boxing Lisp interpreter (~730 lines) with `setjmp`/`longjmp` error handling.
Required all of the tinylisp NaN-boxing fixes plus additional compiler work:
- `setjmp`/`longjmp` mapped to JavaScript `try`/`catch`/`throw` (detect `setjmp` in block
  statements, wrap in `while(true) { try { ... break; } catch { ... continue; } }`)
- BigInt→Number coercion at function call boundaries (`Number(x & 0xFFFFFFFFn)`)
- `switch` expression wrapping for BigInt types (`switch(Number(expr))`)
- `freopen` EOF handling for graceful exit on piped input
- Constant folding for computed array sizes (`try_eval_const` in parser)

All 12 test expressions evaluate correctly:
```
(+ 1 2) => 3
(* 3 4) => 12
(if 1 2 3) => 2
(if () 10 20) => 20
(define x 42) => x, x => 42
(define f (lambda (n) (* n n))) => f, (f 7) => 49
(list 1 2 3) => (1 2 3)
(quote hello) => hello
(begin 1 2 3) => 3
(cond (#t 99)) => 99
```

## Compiler Fixes Applied

1. `defined()` in preprocessor -- process `defined(X)` before macro expansion per C99 6.10.1
2. Global init ordering for function pointers -- emit function pointer registrations before global data
3. `getchar()`/`putchar()`/`puts()` runtime -- added to runtime for character I/O
4. BigInt double representation -- doubles stored as BigInt raw bits to preserve NaN payloads
5. `sscanf` length modifiers -- handle `%lg`, `%lld`, `%n`, exponential notation
6. Signed `long long` codegen -- TY_LLONG uses BigInt (readBigInt64/writeBigInt64)
7. Ternary type coercion -- codegen wraps mismatched branches with appropriate conversions
8. Function pointer call types -- sema derives return type from callee's function pointer type
9. `setjmp`/`longjmp` support -- maps to JS `try`/`catch`/`throw` with while-loop wrapper
10. BigInt→Number coercion at call boundaries -- wrap BigInt args with `Number(x & 0xFFFFFFFFn)`
11. `switch` on BigInt expressions -- wrap switch expression with `Number()` when type is uint64_t
12. `freopen` EOF handling -- exit gracefully when stdin is reopened after EOF
13. Constant folding for array sizes -- `try_eval_const()` evaluates compile-time expressions
14. `__VA_ARGS__` length check -- off-by-one in preprocessor token length comparison
15. POSIX `open`/`read`/`close` -- added as runtime functions + built-in declarations
16. BigInt inc/dec step -- use `BigInt(step)` in pre/post increment for `long long` types
17. Pointer + BigInt arithmetic -- wrap BigInt index with `Number()` in pointer add/subscript
18. BigInt→pointer cast -- `Number(expr & 0xFFFFFFFFn)` when casting `long long` to pointer
19. BigInt main return -- `process.exit(Number(main()))` handles BigInt return values
