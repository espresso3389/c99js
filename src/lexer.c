#include "lexer.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---- Keyword table ---- */
typedef struct { const char *name; TokenKind kind; } Keyword;

static const Keyword keywords[] = {
    {"auto", TK_AUTO}, {"break", TK_BREAK}, {"case", TK_CASE},
    {"char", TK_CHAR}, {"const", TK_CONST}, {"continue", TK_CONTINUE},
    {"default", TK_DEFAULT}, {"do", TK_DO}, {"double", TK_DOUBLE},
    {"else", TK_ELSE}, {"enum", TK_ENUM}, {"extern", TK_EXTERN},
    {"float", TK_FLOAT}, {"for", TK_FOR}, {"goto", TK_GOTO},
    {"if", TK_IF}, {"inline", TK_INLINE}, {"int", TK_INT},
    {"long", TK_LONG}, {"register", TK_REGISTER}, {"restrict", TK_RESTRICT},
    {"return", TK_RETURN}, {"short", TK_SHORT}, {"signed", TK_SIGNED},
    {"sizeof", TK_SIZEOF}, {"static", TK_STATIC}, {"struct", TK_STRUCT},
    {"switch", TK_SWITCH}, {"typedef", TK_TYPEDEF}, {"union", TK_UNION},
    {"unsigned", TK_UNSIGNED}, {"void", TK_VOID}, {"volatile", TK_VOLATILE},
    {"while", TK_WHILE},
    {"_Bool", TK_BOOL}, {"_Complex", TK_COMPLEX}, {"_Imaginary", TK_IMAGINARY},
    {NULL, TK_EOF}
};

static TokenKind lookup_keyword(const char *s, size_t len) {
    for (const Keyword *kw = keywords; kw->name; kw++) {
        if (strlen(kw->name) == len && memcmp(kw->name, s, len) == 0)
            return kw->kind;
    }
    return TK_IDENT;
}

bool token_is_type_keyword(TokenKind kind) {
    switch (kind) {
    case TK_VOID: case TK_CHAR: case TK_SHORT: case TK_INT: case TK_LONG:
    case TK_FLOAT: case TK_DOUBLE: case TK_SIGNED: case TK_UNSIGNED:
    case TK_BOOL: case TK_COMPLEX: case TK_IMAGINARY:
    case TK_STRUCT: case TK_UNION: case TK_ENUM:
    case TK_CONST: case TK_VOLATILE: case TK_RESTRICT:
    case TK_INLINE: case TK_STATIC: case TK_EXTERN: case TK_REGISTER:
    case TK_AUTO: case TK_TYPEDEF:
        return true;
    default:
        return false;
    }
}

/* ---- Token kind to string ---- */
const char *token_kind_str(TokenKind kind) {
    switch (kind) {
    case TK_EOF: return "EOF";
    case TK_INVALID: return "<invalid>";
    case TK_INT_LIT: return "integer literal";
    case TK_FLOAT_LIT: return "float literal";
    case TK_CHAR_LIT: return "char literal";
    case TK_STRING_LIT: return "string literal";
    case TK_IDENT: return "identifier";
    case TK_AUTO: return "auto"; case TK_BREAK: return "break";
    case TK_CASE: return "case"; case TK_CHAR: return "char";
    case TK_CONST: return "const"; case TK_CONTINUE: return "continue";
    case TK_DEFAULT: return "default"; case TK_DO: return "do";
    case TK_DOUBLE: return "double"; case TK_ELSE: return "else";
    case TK_ENUM: return "enum"; case TK_EXTERN: return "extern";
    case TK_FLOAT: return "float"; case TK_FOR: return "for";
    case TK_GOTO: return "goto"; case TK_IF: return "if";
    case TK_INLINE: return "inline"; case TK_INT: return "int";
    case TK_LONG: return "long"; case TK_REGISTER: return "register";
    case TK_RESTRICT: return "restrict"; case TK_RETURN: return "return";
    case TK_SHORT: return "short"; case TK_SIGNED: return "signed";
    case TK_SIZEOF: return "sizeof"; case TK_STATIC: return "static";
    case TK_STRUCT: return "struct"; case TK_SWITCH: return "switch";
    case TK_TYPEDEF: return "typedef"; case TK_UNION: return "union";
    case TK_UNSIGNED: return "unsigned"; case TK_VOID: return "void";
    case TK_VOLATILE: return "volatile"; case TK_WHILE: return "while";
    case TK_BOOL: return "_Bool"; case TK_COMPLEX: return "_Complex";
    case TK_IMAGINARY: return "_Imaginary";
    case TK_LPAREN: return "("; case TK_RPAREN: return ")";
    case TK_LBRACKET: return "["; case TK_RBRACKET: return "]";
    case TK_LBRACE: return "{"; case TK_RBRACE: return "}";
    case TK_DOT: return "."; case TK_ARROW: return "->";
    case TK_INC: return "++"; case TK_DEC: return "--";
    case TK_AMP: return "&"; case TK_STAR: return "*";
    case TK_PLUS: return "+"; case TK_MINUS: return "-";
    case TK_TILDE: return "~"; case TK_BANG: return "!";
    case TK_SLASH: return "/"; case TK_PERCENT: return "%";
    case TK_LSHIFT: return "<<"; case TK_RSHIFT: return ">>";
    case TK_LT: return "<"; case TK_GT: return ">";
    case TK_LE: return "<="; case TK_GE: return ">=";
    case TK_EQ: return "=="; case TK_NE: return "!=";
    case TK_CARET: return "^"; case TK_PIPE: return "|";
    case TK_AND: return "&&"; case TK_OR: return "||";
    case TK_QUESTION: return "?"; case TK_COLON: return ":";
    case TK_SEMICOLON: return ";"; case TK_ELLIPSIS: return "...";
    case TK_ASSIGN: return "=";
    case TK_MUL_ASSIGN: return "*="; case TK_DIV_ASSIGN: return "/=";
    case TK_MOD_ASSIGN: return "%="; case TK_ADD_ASSIGN: return "+=";
    case TK_SUB_ASSIGN: return "-=";
    case TK_LSHIFT_ASSIGN: return "<<="; case TK_RSHIFT_ASSIGN: return ">>=";
    case TK_AND_ASSIGN: return "&="; case TK_XOR_ASSIGN: return "^=";
    case TK_OR_ASSIGN: return "|="; case TK_COMMA: return ",";
    case TK_HASH: return "#"; case TK_HASHHASH: return "##";
    default: return "<unknown>";
    }
}

/* ---- Lexer implementation ---- */
void lexer_init(Lexer *l, const char *src, const char *filename) {
    l->src = src;
    l->p = src;
    l->filename = filename;
    l->line = 1;
    l->col = 1;
    l->prev_col = 1;
    l->has_peek = false;
    l->at_bol = true;  /* start of file is beginning of line */
    memset(&l->cur, 0, sizeof(Token));
    memset(&l->peek, 0, sizeof(Token));
}

static char peek_char(Lexer *l) {
    return *l->p;
}

static char advance(Lexer *l) {
    char c = *l->p;
    if (c == '\n') {
        l->line++;
        l->prev_col = l->col;
        l->col = 1;
    } else {
        l->col++;
    }
    l->p++;
    return c;
}

static bool match_char(Lexer *l, char c) {
    if (*l->p == c) {
        advance(l);
        return true;
    }
    return false;
}

static SrcLoc make_loc(Lexer *l) {
    SrcLoc loc = {l->filename, l->line, l->col};
    return loc;
}

/* Skip whitespace and comments; set at_bol and has_space flags */
static void skip_whitespace(Lexer *l, bool *at_bol, bool *has_space) {
    *at_bol = l->at_bol; /* preserve beginning-of-line state from lexer */
    *has_space = false;
    for (;;) {
        /* Line splicing: backslash-newline */
        if (l->p[0] == '\\' && l->p[1] == '\n') {
            advance(l); advance(l);
            *has_space = true;
            continue;
        }
        if (l->p[0] == '\\' && l->p[1] == '\r' && l->p[2] == '\n') {
            advance(l); advance(l); advance(l);
            *has_space = true;
            continue;
        }

        if (*l->p == ' ' || *l->p == '\t' || *l->p == '\f' || *l->p == '\v') {
            advance(l);
            *has_space = true;
            continue;
        }
        if (*l->p == '\r') {
            advance(l);
            if (*l->p == '\n') advance(l);
            *at_bol = true;
            l->at_bol = true;
            *has_space = true;
            continue;
        }
        if (*l->p == '\n') {
            advance(l);
            *at_bol = true;
            l->at_bol = true;
            *has_space = true;
            continue;
        }
        /* Preprocessor line directive: # linenum "filename" */
        if (*l->p == '#' && *at_bol) {
            /* Skip # followed by number "filename" */
            const char *save = l->p;
            int save_line = l->line;
            int save_col = l->col;
            advance(l); /* skip # */
            while (*l->p == ' ' || *l->p == '\t') advance(l);
            if (isdigit((unsigned char)*l->p)) {
                int newline = 0;
                while (isdigit((unsigned char)*l->p))
                    newline = newline * 10 + (advance(l) - '0');
                while (*l->p == ' ' || *l->p == '\t') advance(l);
                if (*l->p == '"') {
                    advance(l);
                    const char *fname_start = l->p;
                    while (*l->p && *l->p != '"') advance(l);
                    size_t flen = (size_t)(l->p - fname_start);
                    if (flen > 0) {
                        l->filename = str_intern_range(fname_start, l->p);
                    }
                    if (*l->p == '"') advance(l);
                }
                /* Skip rest of line */
                while (*l->p && *l->p != '\n') advance(l);
                l->line = newline;
                *has_space = true;
                *at_bol = true;
                continue;
            }
            /* Not a line directive, restore */
            l->p = save;
            l->line = save_line;
            l->col = save_col;
        }
        /* C-style comment */
        if (l->p[0] == '/' && l->p[1] == '*') {
            advance(l); advance(l);
            while (*l->p) {
                if (l->p[0] == '*' && l->p[1] == '/') {
                    advance(l); advance(l);
                    break;
                }
                advance(l);
            }
            *has_space = true;
            continue;
        }
        /* C++ style comment (allowed in C99) */
        if (l->p[0] == '/' && l->p[1] == '/') {
            advance(l); advance(l);
            while (*l->p && *l->p != '\n') advance(l);
            *has_space = true;
            continue;
        }
        break;
    }
}

/* Parse integer or floating-point literal */
static void lex_number(Lexer *l, Token *t) {
    const char *start = l->p;
    bool is_float = false;
    int base = 10;

    if (l->p[0] == '0') {
        if (l->p[1] == 'x' || l->p[1] == 'X') {
            base = 16;
            advance(l); advance(l);
            while (isxdigit((unsigned char)*l->p)) advance(l);
        } else if (l->p[1] == '.' || l->p[1] == 'e' || l->p[1] == 'E') {
            /* will be handled as float below */
            advance(l);
        } else if (isdigit((unsigned char)l->p[1])) {
            base = 8;
            advance(l);
            while (*l->p >= '0' && *l->p <= '7') advance(l);
        } else {
            advance(l); /* just '0' */
        }
    }

    if (base == 10) {
        while (isdigit((unsigned char)*l->p)) advance(l);
    }

    /* Decimal point */
    if (*l->p == '.' && base != 8) {
        is_float = true;
        advance(l);
        if (base == 16) {
            while (isxdigit((unsigned char)*l->p)) advance(l);
        } else {
            while (isdigit((unsigned char)*l->p)) advance(l);
        }
    }

    /* Exponent */
    if (base == 16 && (*l->p == 'p' || *l->p == 'P')) {
        is_float = true;
        advance(l);
        if (*l->p == '+' || *l->p == '-') advance(l);
        while (isdigit((unsigned char)*l->p)) advance(l);
    } else if (base != 16 && (*l->p == 'e' || *l->p == 'E')) {
        is_float = true;
        advance(l);
        if (*l->p == '+' || *l->p == '-') advance(l);
        while (isdigit((unsigned char)*l->p)) advance(l);
    }

    /* Suffix */
    t->lit_suffix = 0;
    if (is_float) {
        if (*l->p == 'f' || *l->p == 'F') {
            advance(l); /* float suffix */
        } else if (*l->p == 'l' || *l->p == 'L') {
            advance(l); /* long double suffix */
            t->lit_suffix |= LIT_LONG;
        }
        t->kind = TK_FLOAT_LIT;
        t->num.fval = strtod(start, NULL);
    } else {
        /* Integer suffixes */
        for (;;) {
            if ((*l->p == 'u' || *l->p == 'U') && !(t->lit_suffix & LIT_UNSIGNED)) {
                t->lit_suffix |= LIT_UNSIGNED;
                advance(l);
            } else if ((*l->p == 'l' || *l->p == 'L') && !(t->lit_suffix & LIT_LONGLONG)) {
                if (t->lit_suffix & LIT_LONG) {
                    t->lit_suffix = (t->lit_suffix & ~LIT_LONG) | LIT_LONGLONG;
                } else {
                    t->lit_suffix |= LIT_LONG;
                }
                advance(l);
            } else {
                break;
            }
        }
        t->kind = TK_INT_LIT;
        t->num.ival = strtoull(start, NULL, 0);
    }
    t->str = str_intern_range(start, l->p);
    t->str_len = (int)(l->p - start);
}

/* Parse escape character in string/char literal */
static int lex_escape(Lexer *l) {
    advance(l); /* skip backslash */
    int c = advance(l);
    switch (c) {
    case 'a': return '\a';
    case 'b': return '\b';
    case 'f': return '\f';
    case 'n': return '\n';
    case 'r': return '\r';
    case 't': return '\t';
    case 'v': return '\v';
    case '\\': return '\\';
    case '\'': return '\'';
    case '\"': return '\"';
    case '?': return '?';
    case '0': case '1': case '2': case '3':
    case '4': case '5': case '6': case '7': {
        int val = c - '0';
        if (*l->p >= '0' && *l->p <= '7') {
            val = val * 8 + (advance(l) - '0');
            if (*l->p >= '0' && *l->p <= '7')
                val = val * 8 + (advance(l) - '0');
        }
        return val;
    }
    case 'x': {
        int val = 0;
        while (isxdigit((unsigned char)*l->p)) {
            int d = advance(l);
            if (d >= '0' && d <= '9') val = val * 16 + d - '0';
            else if (d >= 'a' && d <= 'f') val = val * 16 + d - 'a' + 10;
            else val = val * 16 + d - 'A' + 10;
        }
        return val;
    }
    default: return c;
    }
}

static void lex_char_lit(Lexer *l, Token *t) {
    t->is_wide = false;
    if (*l->p == 'L') {
        t->is_wide = true;
        advance(l);
    }
    advance(l); /* skip opening quote */
    int c;
    if (*l->p == '\\') {
        c = lex_escape(l);
    } else {
        c = advance(l);
    }
    if (*l->p == '\'') advance(l);
    t->kind = TK_CHAR_LIT;
    t->num.ival = (unsigned long long)c;
    t->str = NULL;
    t->str_len = 0;
}

static void lex_string_lit(Lexer *l, Token *t) {
    Buf buf;
    buf_init(&buf);
    t->is_wide = false;
    if (*l->p == 'L') {
        t->is_wide = true;
        advance(l);
    }
    advance(l); /* skip opening quote */
    while (*l->p && *l->p != '\"') {
        if (*l->p == '\\') {
            int c = lex_escape(l);
            buf_push(&buf, (char)c);
        } else {
            buf_push(&buf, advance(l));
        }
    }
    if (*l->p == '\"') advance(l);
    t->kind = TK_STRING_LIT;
    buf_push(&buf, '\0');
    t->str = str_intern_range(buf.data, buf.data + buf.len - 1);
    t->str_len = (int)(buf.len - 1);
    buf_free(&buf);
}

static void lex_token(Lexer *l, Token *t) {
    bool at_bol, has_space;
    skip_whitespace(l, &at_bol, &has_space);
    t->at_bol = at_bol;
    t->has_space = has_space;
    l->at_bol = false; /* consumed; will be set again by newlines */
    t->loc = make_loc(l);
    t->lit_suffix = 0;
    t->is_wide = false;

    char c = *l->p;

    if (c == '\0') {
        t->kind = TK_EOF;
        t->str = NULL;
        t->str_len = 0;
        return;
    }

    /* Wide char/string */
    if (c == 'L' && (l->p[1] == '\'' || l->p[1] == '\"')) {
        if (l->p[1] == '\'')
            lex_char_lit(l, t);
        else
            lex_string_lit(l, t);
        return;
    }

    /* Identifier or keyword */
    if (isalpha((unsigned char)c) || c == '_') {
        const char *start = l->p;
        while (isalnum((unsigned char)*l->p) || *l->p == '_') advance(l);
        size_t len = (size_t)(l->p - start);
        t->kind = lookup_keyword(start, len);
        t->str = str_intern_range(start, l->p);
        t->str_len = (int)len;
        return;
    }

    /* Number */
    if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)l->p[1]))) {
        lex_number(l, t);
        return;
    }

    /* Character literal */
    if (c == '\'') {
        lex_char_lit(l, t);
        return;
    }

    /* String literal */
    if (c == '\"') {
        lex_string_lit(l, t);
        return;
    }

    /* Punctuators */
    advance(l);
    switch (c) {
    case '(':  t->kind = TK_LPAREN; break;
    case ')':  t->kind = TK_RPAREN; break;
    case '[':  t->kind = TK_LBRACKET; break;
    case ']':  t->kind = TK_RBRACKET; break;
    case '{':  t->kind = TK_LBRACE; break;
    case '}':  t->kind = TK_RBRACE; break;
    case '~':  t->kind = TK_TILDE; break;
    case '?':  t->kind = TK_QUESTION; break;
    case ';':  t->kind = TK_SEMICOLON; break;
    case ',':  t->kind = TK_COMMA; break;
    case '.':
        if (l->p[0] == '.' && l->p[1] == '.') {
            advance(l); advance(l);
            t->kind = TK_ELLIPSIS;
        } else {
            t->kind = TK_DOT;
        }
        break;
    case '#':
        if (match_char(l, '#')) t->kind = TK_HASHHASH;
        else t->kind = TK_HASH;
        break;
    case '+':
        if (match_char(l, '+')) t->kind = TK_INC;
        else if (match_char(l, '=')) t->kind = TK_ADD_ASSIGN;
        else t->kind = TK_PLUS;
        break;
    case '-':
        if (match_char(l, '-')) t->kind = TK_DEC;
        else if (match_char(l, '>')) t->kind = TK_ARROW;
        else if (match_char(l, '=')) t->kind = TK_SUB_ASSIGN;
        else t->kind = TK_MINUS;
        break;
    case '*':
        if (match_char(l, '=')) t->kind = TK_MUL_ASSIGN;
        else t->kind = TK_STAR;
        break;
    case '/':
        if (match_char(l, '=')) t->kind = TK_DIV_ASSIGN;
        else t->kind = TK_SLASH;
        break;
    case '%':
        if (match_char(l, '=')) t->kind = TK_MOD_ASSIGN;
        else t->kind = TK_PERCENT;
        break;
    case '&':
        if (match_char(l, '&')) t->kind = TK_AND;
        else if (match_char(l, '=')) t->kind = TK_AND_ASSIGN;
        else t->kind = TK_AMP;
        break;
    case '|':
        if (match_char(l, '|')) t->kind = TK_OR;
        else if (match_char(l, '=')) t->kind = TK_OR_ASSIGN;
        else t->kind = TK_PIPE;
        break;
    case '^':
        if (match_char(l, '=')) t->kind = TK_XOR_ASSIGN;
        else t->kind = TK_CARET;
        break;
    case '=':
        if (match_char(l, '=')) t->kind = TK_EQ;
        else t->kind = TK_ASSIGN;
        break;
    case '!':
        if (match_char(l, '=')) t->kind = TK_NE;
        else t->kind = TK_BANG;
        break;
    case '<':
        if (match_char(l, '<')) {
            if (match_char(l, '=')) t->kind = TK_LSHIFT_ASSIGN;
            else t->kind = TK_LSHIFT;
        } else if (match_char(l, '=')) {
            t->kind = TK_LE;
        } else {
            t->kind = TK_LT;
        }
        break;
    case '>':
        if (match_char(l, '>')) {
            if (match_char(l, '=')) t->kind = TK_RSHIFT_ASSIGN;
            else t->kind = TK_RSHIFT;
        } else if (match_char(l, '=')) {
            t->kind = TK_GE;
        } else {
            t->kind = TK_GT;
        }
        break;
    case ':':
        t->kind = TK_COLON;
        break;
    default:
        t->kind = TK_INVALID;
        break;
    }
    t->str = NULL;
    t->str_len = 0;
}

void lexer_next(Lexer *l) {
    if (l->has_peek) {
        l->cur = l->peek;
        l->has_peek = false;
    } else {
        lex_token(l, &l->cur);
    }
}

Token lexer_peek(Lexer *l) {
    if (!l->has_peek) {
        lex_token(l, &l->peek);
        l->has_peek = true;
    }
    return l->peek;
}

bool lexer_match(Lexer *l, TokenKind kind) {
    if (l->cur.kind == kind) {
        lexer_next(l);
        return true;
    }
    return false;
}

void lexer_expect(Lexer *l, TokenKind kind) {
    if (l->cur.kind != kind) {
        error_at(l->cur.loc, "expected '%s', got '%s'",
                 token_kind_str(kind), token_kind_str(l->cur.kind));
    }
    lexer_next(l);
}
