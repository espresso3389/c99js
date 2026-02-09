# c99js

A self-hosting C99 compiler that generates JavaScript. Write C, run it with Node.js.

```
$ ./c99js hello.c -o hello.js
$ node hello.js
Hello, world!
```

## Highlights

- **Self-hosting** — c99js can compile its own source code to JavaScript, and the resulting JS compiler produces byte-identical output
- **Cross-platform** — builds and runs on Linux, macOS, and Windows
- **No dependencies** — the compiler is pure C99; the generated code runs on Node.js (or Bun)
- **~7,000 lines of C** — compact, readable implementation

## Building

Any C99 compiler works. Pick one:

```bash
# Clang
clang -std=c99 -O2 -o c99js src/util.c src/type.c src/lexer.c src/ast.c \
  src/symtab.c src/preprocess.c src/parser.c src/sema.c src/codegen.c src/main.c

# GCC
gcc -std=c99 -O2 -o c99js src/util.c src/type.c src/lexer.c src/ast.c \
  src/symtab.c src/preprocess.c src/parser.c src/sema.c src/codegen.c src/main.c

# Zig
zig build -Doptimize=ReleaseFast
```

## Usage

```
Usage: c99js [options] <input.c> [-o <output.js>]
Options:
  -o <file>        Output file (default: stdout)
  -I <dir>         Add include search path
  -D <name>=<val>  Define preprocessor macro
  -E               Preprocess only
  --dump-ast       Print AST (for debugging)
  -h, --help       Show this help
```

### Example

```c
// hello.c
#include <stdio.h>

int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

int main(void) {
    printf("factorial(10) = %d\n", factorial(10));
    return 0;
}
```

```bash
$ ./c99js hello.c -o hello.js
$ node hello.js
factorial(10) = 3628800
```

## Self-Compilation

c99js can compile itself. The `selfcompile.c` unity build includes all compiler sources into a single translation unit:

```bash
# Step 1: Build native compiler
clang -std=c99 -O2 -o c99js src/*.c

# Step 2: Compile the compiler to JavaScript
./c99js selfcompile.c -o selfcompile.js

# Step 3: Use the JS compiler to compile itself
node selfcompile.js selfcompile.c -o selfcompile2.js

# Step 4: Verify — the two JS files are byte-identical
diff <(tr -d '\r' < selfcompile.js) <(tr -d '\r' < selfcompile2.js)
# (no output — identical)
```

## Architecture

The compiler follows a traditional pipeline:

```
Source (.c) → Preprocessor → Lexer → Parser → Sema → Codegen → JavaScript (.js)
```

| Stage | File | Description |
|---|---|---|
| Preprocessor | `preprocess.c` | `#include`, `#define`, `#ifdef`, macro expansion |
| Lexer | `lexer.c` | Tokenization with line/column tracking |
| Parser | `parser.c` | Recursive descent, builds AST |
| Semantic Analysis | `sema.c` | Type checking, implicit casts, symbol resolution |
| Code Generation | `codegen.c` | Two-pass: collects string literals, then emits JS |
| Runtime | `runtime/runtime.js` | Memory model, stdlib implementations |
| Utilities | `util.c` | Arena allocator, string interning, error reporting |

### Memory Model

Generated programs run on a virtual memory system implemented in `runtime.js`:

```
┌──────────────────────────────┐  Address 0
│  NULL guard (4 bytes)        │
├──────────────────────────────┤
│  Global variables            │
├──────────────────────────────┤
│  Heap (grows ↓)              │
│  malloc / calloc / realloc   │
│            ...               │
│  Stack (grows ↑)             │
│  function frames             │
├──────────────────────────────┤  16 MB (default)
└──────────────────────────────┘
```

- **16 MB** ArrayBuffer with little-endian byte order
- **Stack** grows downward from the top (1 MB reserved)
- **Heap** uses a first-fit allocator with free-list coalescing
- **Function pointers** stored in a side table (JS functions can't live in ArrayBuffer)

## Supported C99 Features

### Types
- `void`, `_Bool`, `char`, `short`, `int`, `long`, `long long`, `float`, `double`
- `unsigned` variants, `enum`, `typedef`
- Pointers, arrays, structs, unions (including anonymous members)
- Function pointers
- Flexible array members

### Statements
- `if`/`else`, `while`, `do`-`while`, `for`
- `switch`/`case`/`default`
- `break`, `continue`, `return`, `goto`
- Compound statements (blocks)

### Expressions
- Arithmetic, bitwise, logical, relational operators
- Assignment operators (`=`, `+=`, `-=`, etc.)
- Ternary (`?:`), comma, `sizeof`, casts
- Address-of (`&`), dereference (`*`), member access (`.`, `->`)
- Pre/post increment/decrement
- Compound literals

### Declarations
- Global and local variables with initializers
- Aggregate initialization (`{...}`) and designated initializers (`.field=`, `[i]=`)
- String literal initialization for `char` arrays
- `static`, `extern`, `const`, `volatile`, `restrict`, `inline`
- Variadic functions (`va_start`, `va_arg`, `va_end`, `va_copy`)

### Preprocessor
- `#include` (with `-I` search paths)
- `#define` / `#undef` (object-like and function-like macros)
- `#if` / `#ifdef` / `#ifndef` / `#elif` / `#else` / `#endif`
- `#error`, `#pragma` (ignored)
- `__FILE__`, `__LINE__`, `__DATE__`, `__TIME__`

## Standard Library

The runtime (`runtime/runtime.js`) provides implementations of common C standard library functions:

| Category | Functions |
|---|---|
| **I/O** | `printf`, `fprintf`, `sprintf`, `snprintf`, `scanf`, `sscanf`, `puts`, `putchar`, `getchar` |
| **File I/O** | `fopen`, `fclose`, `fread`, `fwrite`, `fgets`, `fputs`, `fgetc`, `fputc`, `fseek`, `ftell`, `feof`, `rewind` |
| **Memory** | `malloc`, `calloc`, `realloc`, `free` |
| **Strings** | `strlen`, `strcpy`, `strncpy`, `strcat`, `strncat`, `strcmp`, `strncmp`, `strchr`, `strrchr`, `strstr`, `strdup` |
| **Memory ops** | `memcpy`, `memmove`, `memset`, `memchr`, `memcmp` |
| **Conversion** | `atoi`, `atof`, `strtol`, `strtoul`, `strtod`, `strtoll`, `strtoull` |
| **Math** | `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sqrt`, `pow`, `fabs`, `ceil`, `floor`, `fmod`, `log`, `log10`, `exp` |
| **Ctype** | `isalpha`, `isdigit`, `isalnum`, `isspace`, `isupper`, `islower`, `toupper`, `tolower` |
| **Stdlib** | `abs`, `labs`, `rand`, `srand`, `exit`, `abort`, `qsort`, `bsearch` |
| **Time** | `time`, `clock`, `difftime`, `localtime`, `strftime` |

## Testing

```bash
# Run all 19 primitive tests
bash test/run_tests.sh ./c99js node

# Run the full self-compilation verification
bash test/test_selfcompile.sh node
```

The self-compilation test verifies:
1. Native compiler builds successfully
2. Native compiler compiles itself to JS (`selfcompile.js`)
3. JS compiler compiles itself to JS (`selfcompile2.js`)
4. `selfcompile.js` and `selfcompile2.js` are byte-identical
5. All primitive tests pass with the self-compiled compiler

CI runs on every push across Ubuntu, macOS, and Windows.

## Project Structure

```
c99js/
├── src/                    # Compiler source (~7,000 lines)
│   ├── main.c              # Driver and builtin registration
│   ├── lexer.c/h           # Tokenizer
│   ├── preprocess.c/h      # Preprocessor
│   ├── parser.c/h          # Recursive descent parser
│   ├── ast.c/h             # AST node definitions
│   ├── type.c/h            # Type system
│   ├── symtab.c/h          # Symbol table with scoping
│   ├── sema.c/h            # Semantic analysis
│   ├── codegen.c/h         # JavaScript code generation
│   └── util.c/h            # Arena allocator, buffers, errors
├── runtime/
│   └── runtime.js          # JS runtime (memory, stdlib)
├── test/
│   ├── test_*.c            # Test programs
│   ├── expected/           # Expected outputs
│   ├── run_tests.sh        # Test runner
│   └── test_selfcompile.sh # Self-compilation test
├── selfcompile.c           # Unity build for self-compilation
├── build.zig               # Zig build script
└── .github/workflows/
    └── ci.yml              # CI (Linux, macOS, Windows)
```

## License

MIT
