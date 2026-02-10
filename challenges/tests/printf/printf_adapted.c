/*
 * Adapted version of mpaland/printf for c99js transpiler.
 *
 * Original: (c) Marco Paland (info@paland.com) 2014-2019, MIT License
 *
 * Major adaptations for c99js:
 * - c99js does not support va_arg() -- variadic argument extraction.
 * - Redesigned to use an argument array (int args[], int nargs) instead of va_list.
 * - Floats passed as two ints (lo, hi) representing IEEE754 double bits.
 * - Removed: long long, exponential notation, ptrdiff_t, pointer format.
 * - Uses typedef aliases for multi-word types.
 * - The public snprintf_ is implemented as a non-variadic function taking
 *   pre-parsed arguments, with specific wrappers for common cases.
 */

#include <stdbool.h>
#include <stddef.h>

/* Type aliases to work around c99js multi-word type limitations */
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned char uchar;

/* Buffer sizes */
#define PRINTF_NTOA_BUFFER_SIZE    32U
#define PRINTF_FTOA_BUFFER_SIZE    32U
#define PRINTF_DEFAULT_FLOAT_PRECISION  6U
#define PRINTF_MAX_FLOAT  1e9

/* Enable float support */
#define PRINTF_SUPPORT_FLOAT

/* Internal flag definitions */
#define FLAGS_ZEROPAD   (1U <<  0U)
#define FLAGS_LEFT      (1U <<  1U)
#define FLAGS_PLUS      (1U <<  2U)
#define FLAGS_SPACE     (1U <<  3U)
#define FLAGS_HASH      (1U <<  4U)
#define FLAGS_UPPERCASE (1U <<  5U)
#define FLAGS_CHAR      (1U <<  6U)
#define FLAGS_SHORT     (1U <<  7U)
#define FLAGS_LONG      (1U <<  8U)
#define FLAGS_LONG_LONG (1U <<  9U)
#define FLAGS_PRECISION (1U << 10U)
#define FLAGS_ADAPT_EXP (1U << 11U)

/* DBL_MAX for NaN/inf detection -- use negative constant directly to avoid
   c99js issues with negating float bit patterns */
#define MY_DBL_MAX 1.7976931348623157e+308
#define MY_NEG_DBL_MAX (-1.7976931348623157e+308)
#define MY_NEG_MAX_FLOAT (-1000000000.0)

/* output function type */
typedef void (*out_fct_type)(char character, void* buffer, size_t idx, size_t maxlen);

/* --- Helper functions to avoid problematic casts --- */
static char to_char(int v) { return v; }
static uint to_uint(int v) { if (v < 0) return 0; return v; }
static ulong int_to_ulong(int v) { return v; }
static uint ulong_to_uint(ulong v) { return v; }
static double neg_double(double v) { return 0.0 - v; }

/* internal buffer output */
static void _out_buffer(char character, void* buffer, size_t idx, size_t maxlen)
{
  if (idx < maxlen) {
    ((char*)buffer)[idx] = character;
  }
}

/* internal null output */
static void _out_null(char character, void* buffer, size_t idx, size_t maxlen)
{
  (void)character; (void)buffer; (void)idx; (void)maxlen;
}

/* internal _putchar wrapper */
static void _out_char(char character, void* buffer, size_t idx, size_t maxlen)
{
  (void)buffer; (void)idx; (void)maxlen;
  if (character) {
    putchar(character);
  }
}

/* internal secure strlen */
static uint _strnlen_s(const char* str, size_t maxsize)
{
  const char* s;
  for (s = str; *s && maxsize--; ++s);
  return (uint)(s - str);
}

/* internal test if char is a digit (0-9) */
static bool _is_digit(char ch)
{
  return (ch >= '0') && (ch <= '9');
}

/* internal ASCII string to unsigned int conversion */
static uint _atoi(const char** str)
{
  uint i = 0U;
  while (_is_digit(**str)) {
    i = i * 10U + (uint)(*((*str)++) - '0');
  }
  return i;
}

/* output the specified string in reverse, taking care of any zero-padding */
static size_t _out_rev(out_fct_type out, char* buffer, size_t idx, size_t maxlen, const char* buf, size_t len, uint width, uint flags)
{
  const size_t start_idx = idx;

  /* pad spaces up to given width */
  if (!(flags & FLAGS_LEFT) && !(flags & FLAGS_ZEROPAD)) {
    size_t i;
    for (i = len; i < width; i++) {
      out(' ', buffer, idx++, maxlen);
    }
  }

  /* reverse string */
  while (len) {
    out(buf[--len], buffer, idx++, maxlen);
  }

  /* append pad spaces up to given width */
  if (flags & FLAGS_LEFT) {
    while (idx - start_idx < width) {
      out(' ', buffer, idx++, maxlen);
    }
  }

  return idx;
}

/* internal itoa format */
static size_t _ntoa_format(out_fct_type out, char* buffer, size_t idx, size_t maxlen, char* buf, size_t len, bool negative, uint base, uint prec, uint width, uint flags)
{
  /* pad leading zeros */
  if (!(flags & FLAGS_LEFT)) {
    if (width && (flags & FLAGS_ZEROPAD) && (negative || (flags & (FLAGS_PLUS | FLAGS_SPACE)))) {
      width--;
    }
    while ((len < prec) && (len < PRINTF_NTOA_BUFFER_SIZE)) {
      buf[len++] = '0';
    }
    while ((flags & FLAGS_ZEROPAD) && (len < width) && (len < PRINTF_NTOA_BUFFER_SIZE)) {
      buf[len++] = '0';
    }
  }

  /* handle hash */
  if (flags & FLAGS_HASH) {
    if (!(flags & FLAGS_PRECISION) && len && ((len == prec) || (len == width))) {
      len--;
      if (len && (base == 16U)) {
        len--;
      }
    }
    if ((base == 16U) && !(flags & FLAGS_UPPERCASE) && (len < PRINTF_NTOA_BUFFER_SIZE)) {
      buf[len++] = 'x';
    }
    else if ((base == 16U) && (flags & FLAGS_UPPERCASE) && (len < PRINTF_NTOA_BUFFER_SIZE)) {
      buf[len++] = 'X';
    }
    else if ((base == 2U) && (len < PRINTF_NTOA_BUFFER_SIZE)) {
      buf[len++] = 'b';
    }
    if (len < PRINTF_NTOA_BUFFER_SIZE) {
      buf[len++] = '0';
    }
  }

  if (len < PRINTF_NTOA_BUFFER_SIZE) {
    if (negative) {
      buf[len++] = '-';
    }
    else if (flags & FLAGS_PLUS) {
      buf[len++] = '+';
    }
    else if (flags & FLAGS_SPACE) {
      buf[len++] = ' ';
    }
  }

  return _out_rev(out, buffer, idx, maxlen, buf, len, width, flags);
}

/* internal itoa for 'long' type */
static size_t _ntoa_long(out_fct_type out, char* buffer, size_t idx, size_t maxlen, ulong value, bool negative, ulong base, uint prec, uint width, uint flags)
{
  char buf[PRINTF_NTOA_BUFFER_SIZE];
  size_t len = 0U;

  /* no hash for 0 values */
  if (!value) {
    flags &= ~FLAGS_HASH;
  }

  /* write if precision != 0 and value is != 0 */
  if (!(flags & FLAGS_PRECISION) || value) {
    do {
      const char digit = to_char(value % base);
      buf[len++] = digit < 10 ? '0' + digit : (flags & FLAGS_UPPERCASE ? 'A' : 'a') + digit - 10;
      value = value / base;
    } while (value && (len < PRINTF_NTOA_BUFFER_SIZE));
  }

  return _ntoa_format(out, buffer, idx, maxlen, buf, len, negative, ulong_to_uint(base), prec, width, flags);
}

#if defined(PRINTF_SUPPORT_FLOAT)

/* internal ftoa for fixed decimal floating point */
static size_t _ftoa(out_fct_type out, char* buffer, size_t idx, size_t maxlen, double value, uint prec, uint width, uint flags)
{
  char buf[PRINTF_FTOA_BUFFER_SIZE];
  size_t len  = 0U;
  double diff = 0.0;

  /* powers of 10 - use explicit .0 so c99js emits float bit patterns */
  static const double pow10[] = { 1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0, 1000000.0, 10000000.0, 100000000.0, 1000000000.0 };

  /* test for special values */
  if (value != value)
    return _out_rev(out, buffer, idx, maxlen, "nan", 3, width, flags);
  if (value < MY_NEG_DBL_MAX)
    return _out_rev(out, buffer, idx, maxlen, "fni-", 4, width, flags);
  if (value > MY_DBL_MAX)
    return _out_rev(out, buffer, idx, maxlen, (flags & FLAGS_PLUS) ? "fni+" : "fni", (flags & FLAGS_PLUS) ? 4U : 3U, width, flags);

  /* test for very large values */
  if ((value > PRINTF_MAX_FLOAT) || (value < MY_NEG_MAX_FLOAT)) {
    return 0U;
  }

  /* test for negative */
  bool negative = false;
  if (value < 0) {
    negative = true;
    value = 0 - value;
  }

  /* set default precision, if not set explicitly */
  if (!(flags & FLAGS_PRECISION)) {
    prec = PRINTF_DEFAULT_FLOAT_PRECISION;
  }
  /* limit precision to 9 */
  while ((len < PRINTF_FTOA_BUFFER_SIZE) && (prec > 9U)) {
    buf[len++] = '0';
    prec--;
  }

  int whole = (int)value;
  double tmp = (value - whole) * pow10[prec];
  ulong frac = (ulong)tmp;
  diff = tmp - frac;

  if (diff > 0.5) {
    ++frac;
    if (frac >= pow10[prec]) {
      frac = 0;
      ++whole;
    }
  }
  else if (diff < 0.5) {
    /* do nothing */
  }
  else if ((frac == 0U) || (frac & 1U)) {
    ++frac;
  }

  if (prec == 0U) {
    diff = value - (double)whole;
    if ((!(diff < 0.5) || (diff > 0.5)) && (whole & 1)) {
      ++whole;
    }
  }
  else {
    uint count = prec;
    while (len < PRINTF_FTOA_BUFFER_SIZE) {
      --count;
      buf[len++] = to_char(48U + (frac % 10U));
      frac = frac / 10U;
      if (!frac) {
        break;
      }
    }
    while ((len < PRINTF_FTOA_BUFFER_SIZE) && (count-- > 0U)) {
      buf[len++] = '0';
    }
    if (len < PRINTF_FTOA_BUFFER_SIZE) {
      buf[len++] = '.';
    }
  }

  while (len < PRINTF_FTOA_BUFFER_SIZE) {
    buf[len++] = to_char(48 + (whole % 10));
    whole = whole / 10;
    if (!whole) {
      break;
    }
  }

  if (!(flags & FLAGS_LEFT) && (flags & FLAGS_ZEROPAD)) {
    if (width && (negative || (flags & (FLAGS_PLUS | FLAGS_SPACE)))) {
      width--;
    }
    while ((len < width) && (len < PRINTF_FTOA_BUFFER_SIZE)) {
      buf[len++] = '0';
    }
  }

  if (len < PRINTF_FTOA_BUFFER_SIZE) {
    if (negative) {
      buf[len++] = '-';
    }
    else if (flags & FLAGS_PLUS) {
      buf[len++] = '+';
    }
    else if (flags & FLAGS_SPACE) {
      buf[len++] = ' ';
    }
  }

  return _out_rev(out, buffer, idx, maxlen, buf, len, width, flags);
}

#endif /* PRINTF_SUPPORT_FLOAT */


/*
 * Argument union for passing different types through a uniform array.
 * In c99js, we will pass int args for ints/chars/strings(ptrs)
 * and double args for floats.
 */
#define ARG_TYPE_INT    0
#define ARG_TYPE_UINT   1
#define ARG_TYPE_DOUBLE 2
#define ARG_TYPE_STRING 3
#define ARG_TYPE_CHAR   4
#define MAX_ARGS 16

typedef struct {
  int type;
  int ival;
  double dval;
  const char *sval;
} arg_t;


/* internal format using arg array instead of va_list */
static int _format_args(out_fct_type out, char* buffer, const size_t maxlen, const char* format, arg_t* args, int nargs)
{
  uint flags, width, precision, n;
  size_t idx = 0U;
  int ai = 0; /* argument index */

  if (!buffer) {
    out = _out_null;
  }

  while (*format)
  {
    if (*format != '%') {
      out(*format, buffer, idx++, maxlen);
      format++;
      continue;
    }
    else {
      format++;
    }

    /* evaluate flags */
    flags = 0U;
    do {
      switch (*format) {
        case '0': flags |= FLAGS_ZEROPAD; format++; n = 1U; break;
        case '-': flags |= FLAGS_LEFT;    format++; n = 1U; break;
        case '+': flags |= FLAGS_PLUS;    format++; n = 1U; break;
        case ' ': flags |= FLAGS_SPACE;   format++; n = 1U; break;
        case '#': flags |= FLAGS_HASH;    format++; n = 1U; break;
        default :                                   n = 0U; break;
      }
    } while (n);

    /* evaluate width field */
    width = 0U;
    if (_is_digit(*format)) {
      width = _atoi(&format);
    }
    else if (*format == '*') {
      int w = 0;
      if (ai < nargs) { w = args[ai].ival; ai++; }
      if (w < 0) {
        flags |= FLAGS_LEFT;
        width = to_uint(-w);
      }
      else {
        width = to_uint(w);
      }
      format++;
    }

    /* evaluate precision field */
    precision = 0U;
    if (*format == '.') {
      flags |= FLAGS_PRECISION;
      format++;
      if (_is_digit(*format)) {
        precision = _atoi(&format);
      }
      else if (*format == '*') {
        int prec = 0;
        if (ai < nargs) { prec = args[ai].ival; ai++; }
        precision = prec > 0 ? to_uint(prec) : 0U;
        format++;
      }
    }

    /* evaluate length field */
    switch (*format) {
      case 'l' :
        flags |= FLAGS_LONG;
        format++;
        if (*format == 'l') {
          flags |= FLAGS_LONG_LONG;
          format++;
        }
        break;
      case 'h' :
        flags |= FLAGS_SHORT;
        format++;
        if (*format == 'h') {
          flags |= FLAGS_CHAR;
          format++;
        }
        break;
      case 'z' :
        flags |= FLAGS_LONG;
        format++;
        break;
      default :
        break;
    }

    /* evaluate specifier */
    switch (*format) {
      case 'd' :
      case 'i' :
      case 'u' :
      case 'x' :
      case 'X' :
      case 'o' :
      case 'b' : {
        uint base;
        if (*format == 'x' || *format == 'X') {
          base = 16U;
        }
        else if (*format == 'o') {
          base =  8U;
        }
        else if (*format == 'b') {
          base =  2U;
        }
        else {
          base = 10U;
          flags &= ~FLAGS_HASH;
        }
        if (*format == 'X') {
          flags |= FLAGS_UPPERCASE;
        }
        if ((*format != 'i') && (*format != 'd')) {
          flags &= ~(FLAGS_PLUS | FLAGS_SPACE);
        }
        if (flags & FLAGS_PRECISION) {
          flags &= ~FLAGS_ZEROPAD;
        }

        if ((*format == 'i') || (*format == 'd')) {
          /* signed */
          int value = 0;
          if (ai < nargs) { value = args[ai].ival; ai++; }
          ulong absval;
          if (value > 0) { absval = int_to_ulong(value); }
          else { absval = int_to_ulong(0 - value); }
          idx = _ntoa_long(out, buffer, idx, maxlen, absval, value < 0, base, precision, width, flags);
        }
        else {
          /* unsigned */
          uint uval = 0;
          if (ai < nargs) { uval = (uint)args[ai].ival; ai++; }
          idx = _ntoa_long(out, buffer, idx, maxlen, uval, false, base, precision, width, flags);
        }
        format++;
        break;
      }
#if defined(PRINTF_SUPPORT_FLOAT)
      case 'f' :
      case 'F' :
        if (*format == 'F') flags |= FLAGS_UPPERCASE;
        {
          double dval = 0.0;
          if (ai < nargs) { dval = args[ai].dval; ai++; }
          idx = _ftoa(out, buffer, idx, maxlen, dval, precision, width, flags);
        }
        format++;
        break;
#endif
      case 'c' : {
        uint l = 1U;
        if (!(flags & FLAGS_LEFT)) {
          while (l++ < width) {
            out(' ', buffer, idx++, maxlen);
          }
        }
        {
          int cval = 0;
          if (ai < nargs) { cval = args[ai].ival; ai++; }
          out(to_char(cval), buffer, idx++, maxlen);
        }
        if (flags & FLAGS_LEFT) {
          while (l++ < width) {
            out(' ', buffer, idx++, maxlen);
          }
        }
        format++;
        break;
      }

      case 's' : {
        const char* p = "";
        if (ai < nargs) { p = args[ai].sval; ai++; }
        uint l = _strnlen_s(p, precision ? precision : (size_t)-1);
        if (flags & FLAGS_PRECISION) {
          l = (l < precision ? l : precision);
        }
        if (!(flags & FLAGS_LEFT)) {
          while (l++ < width) {
            out(' ', buffer, idx++, maxlen);
          }
        }
        while ((*p != 0) && (!(flags & FLAGS_PRECISION) || precision--)) {
          out(*(p++), buffer, idx++, maxlen);
        }
        if (flags & FLAGS_LEFT) {
          while (l++ < width) {
            out(' ', buffer, idx++, maxlen);
          }
        }
        format++;
        break;
      }

      case '%' :
        out('%', buffer, idx++, maxlen);
        format++;
        break;

      default :
        out(*format, buffer, idx++, maxlen);
        format++;
        break;
    }
  }

  /* termination */
  out(to_char(0), buffer, idx < maxlen ? idx : maxlen - 1U, maxlen);

  /* return written chars without terminating \0 */
  return (int)idx;
}


/* ============================================================
 * Convenience wrappers - these build the arg array and call
 * _format_args directly, avoiding any use of va_list/va_arg.
 * ============================================================ */

/* snprintf with 0 extra args (just format string) */
static int snprintf_0(char *buf, size_t sz, const char *fmt) {
  arg_t args[1];
  return _format_args(_out_buffer, buf, sz, fmt, args, 0);
}

/* snprintf with 1 int arg */
static int snprintf_i(char *buf, size_t sz, const char *fmt, int a0) {
  arg_t args[1];
  args[0].ival = a0;
  return _format_args(_out_buffer, buf, sz, fmt, args, 1);
}

/* snprintf with 1 uint arg */
static int snprintf_u(char *buf, size_t sz, const char *fmt, uint a0) {
  arg_t args[1];
  args[0].ival = a0;
  return _format_args(_out_buffer, buf, sz, fmt, args, 1);
}

/* snprintf with 1 string arg */
static int snprintf_s(char *buf, size_t sz, const char *fmt, const char *a0) {
  arg_t args[1];
  args[0].sval = a0;
  return _format_args(_out_buffer, buf, sz, fmt, args, 1);
}

/* snprintf with 1 double arg */
static int snprintf_f(char *buf, size_t sz, const char *fmt, double a0) {
  arg_t args[1];
  args[0].dval = a0;
  return _format_args(_out_buffer, buf, sz, fmt, args, 1);
}

/* snprintf with 3 int args */
static int snprintf_iii(char *buf, size_t sz, const char *fmt, int a0, int a1, int a2) {
  arg_t args[3];
  args[0].ival = a0;
  args[1].ival = a1;
  args[2].ival = a2;
  return _format_args(_out_buffer, buf, sz, fmt, args, 3);
}

/* printf_ using puts/putchar - outputs a format string with arg array */
static int printf_args(const char* fmt, arg_t* args, int nargs) {
  char tmp[1];
  return _format_args(_out_char, tmp, (size_t)-1, fmt, args, nargs);
}

/* printf with 2 string args */
static int printf_ss(const char* fmt, const char *a0, const char *a1) {
  char tmp[1];
  arg_t args[2];
  args[0].sval = a0;
  args[1].sval = a1;
  return _format_args(_out_char, tmp, (size_t)-1, fmt, args, 2);
}

/* printf with 1 string + 2 string args (3 strings total) */
static int printf_sss(const char* fmt, const char *a0, const char *a1, const char *a2) {
  char tmp[1];
  arg_t args[3];
  args[0].sval = a0;
  args[1].sval = a1;
  args[2].sval = a2;
  return _format_args(_out_char, tmp, (size_t)-1, fmt, args, 3);
}

/* printf with 2 int args */
static int printf_ii(const char* fmt, int a0, int a1) {
  char tmp[1];
  arg_t args[2];
  args[0].ival = a0;
  args[1].ival = a1;
  return _format_args(_out_char, tmp, (size_t)-1, fmt, args, 2);
}
