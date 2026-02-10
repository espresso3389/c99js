# c99js Showcases

Real-world and non-trivial C projects that compile to JavaScript with c99js.

---

## voxtral.c --- Mistral AI Speech-to-Text Engine

**Project:** [antirez/voxtral.c](https://github.com/antirez/voxtral.c)
**Author:** Salvatore Sanfilippo (creator of Redis)
**Size:** ~5,000 lines of C across 8 source files

voxtral.c is a pure C implementation of the Mistral AI Voxtral Realtime 4B
speech-to-text model. It implements a full neural network inference pipeline:
WAV input, mel spectrogram computation, convolutional encoder, transformer
decoder with KV cache, and BPE tokenizer --- all in C99.

### What works

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

### Build method

voxtral.c uses POSIX-specific APIs (`mmap`, `open`, `fstat`, `sigaction`)
that are not available in JavaScript. A thin compatibility layer provides
these through standard C I/O:

- **`mmap`** is implemented as `malloc` + `fread` (read the entire file
  into a heap buffer)
- **`open`/`close`/`fstat`** are implemented via `fopen`/`fclose`/`ftell`
  with a small fd-to-FILE* table
- **`sigaction`/`sigemptyset`** are provided as no-op stubs
- **`gettimeofday`** returns zeros (timing is cosmetic)
- **`cblas_sgemm`** is replaced with a triple-nested loop

The original voxtral source files are compiled without modification. A
36-line unity build file (`voxtral_unity.c`) includes all sources and
provides the BLAS shim.

### C features exercised

- Structs with arrays of structs (nested model weight definitions)
- `uint16_t` BF16/F16 to float conversion via `memcpy` bit manipulation
- Extensive use of `float` math: `sqrtf`, `expf`, `logf`, `sinf`, `cosf`,
  `tanhf`, `powf`, `ceilf`, `fminf`
- `mmap`-based file loading (via compat layer)
- `snprintf`, `fprintf`, `perror`, `fflush`
- Signal handling (`sig_atomic_t`, `volatile`)
- Variadic argument parsing (`atof`, `strcmp` on `argv`)
- Large-scale matrix operations (matmul, attention, softmax, RMS norm)
- Incremental streaming with ring buffers and state machines
- `%zu` format specifier for `size_t`

---

## c99js --- Self-Compilation (Bootstrapping)

**Size:** ~5,500 lines of C (10 source files)

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

### C features exercised

- Recursive descent parser with a full AST
- Arena allocator for memory management
- Hash tables (symbol table, macro table)
- Variadic functions (`va_start`, `va_arg`, `va_end`)
- Function pointers
- Preprocessor with macro expansion, `#if` expression evaluation,
  stringification (`#`), and token pasting (`##`)
- File I/O (`fopen`, `fread`, `fseek`, `ftell`)
- `qsort` with function pointer comparators
- Extensive `snprintf` and `fprintf` formatting

---

## C-Simple-JSON-Parser

**Project:** [C-Simple-JSON-Parser](https://github.com/niclas-ahden/C-Simple-JSON-Parser)
**Size:** ~1,300 lines of C

A full JSON parser that handles objects, arrays, strings, numbers, booleans,
and null values. c99js compiles and runs it correctly, parsing and
pretty-printing JSON documents.

```
$ c99js test/test_json.c -o test_json.js
$ node test_json.js
{
  "name": "Alice",
  "age": 30,
  "active": true
}
...
All tests passed!
```

### C features exercised

- Heavy use of macros for generic result types (`result(T)`, `typed(T)`)
- Union types with tag-based dispatch
- Recursive data structures (JSON values containing JSON values)
- Dynamic memory allocation for tree construction
- String processing and escape sequence handling
- `clock()` for timing

---

## Summary

| Project | Lines of C | Files | Domain | Status |
|---|---|---|---|---|
| **voxtral.c** | ~5,000 | 8 | ML inference (speech-to-text) | Compiles and runs |
| **c99js** (self) | ~5,500 | 10 | Compiler | Compiles and self-verifies |
| **C-Simple-JSON-Parser** | ~1,300 | 2 | Data parsing | Compiles and runs |

All three projects compile with zero modifications to their source code.
