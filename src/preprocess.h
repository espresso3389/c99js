#ifndef C99JS_PREPROCESS_H
#define C99JS_PREPROCESS_H

#include "util.h"

/* Preprocess a C source file, expanding all preprocessor directives.
 * Returns the preprocessed source as a newly allocated string.
 * include_paths is a NULL-terminated array of directories to search for #include.
 */
char *preprocess(const char *src, const char *filename,
                 const char **include_paths, Arena *arena);

/* Add a predefined macro (e.g., from command line -D) */
void preprocess_define(const char *name, const char *value);

#endif /* C99JS_PREPROCESS_H */
