#ifndef C99JS_LEXER_H
#define C99JS_LEXER_H

#include "util.h"

/* Token kinds */
typedef enum {
    /* Special */
    TK_EOF = 0,
    TK_INVALID,

    /* Literals */
    TK_INT_LIT,        /* integer constant */
    TK_FLOAT_LIT,      /* floating constant */
    TK_CHAR_LIT,       /* character constant */
    TK_STRING_LIT,     /* string literal */

    /* Identifier */
    TK_IDENT,

    /* Keywords (C99) */
    TK_AUTO, TK_BREAK, TK_CASE, TK_CHAR, TK_CONST,
    TK_CONTINUE, TK_DEFAULT, TK_DO, TK_DOUBLE, TK_ELSE,
    TK_ENUM, TK_EXTERN, TK_FLOAT, TK_FOR, TK_GOTO,
    TK_IF, TK_INLINE, TK_INT, TK_LONG, TK_REGISTER,
    TK_RESTRICT, TK_RETURN, TK_SHORT, TK_SIGNED, TK_SIZEOF,
    TK_STATIC, TK_STRUCT, TK_SWITCH, TK_TYPEDEF, TK_UNION,
    TK_UNSIGNED, TK_VOID, TK_VOLATILE, TK_WHILE,
    TK_BOOL,           /* _Bool */
    TK_COMPLEX,         /* _Complex */
    TK_IMAGINARY,       /* _Imaginary */

    /* Punctuators */
    TK_LPAREN, TK_RPAREN,     /* ( ) */
    TK_LBRACKET, TK_RBRACKET, /* [ ] */
    TK_LBRACE, TK_RBRACE,     /* { } */
    TK_DOT,                    /* . */
    TK_ARROW,                  /* -> */
    TK_INC, TK_DEC,           /* ++ -- */
    TK_AMP,                    /* & */
    TK_STAR,                   /* * */
    TK_PLUS, TK_MINUS,        /* + - */
    TK_TILDE,                  /* ~ */
    TK_BANG,                   /* ! */
    TK_SLASH,                  /* / */
    TK_PERCENT,                /* % */
    TK_LSHIFT, TK_RSHIFT,     /* << >> */
    TK_LT, TK_GT,             /* < > */
    TK_LE, TK_GE,             /* <= >= */
    TK_EQ, TK_NE,             /* == != */
    TK_CARET,                  /* ^ */
    TK_PIPE,                   /* | */
    TK_AND, TK_OR,            /* && || */
    TK_QUESTION,               /* ? */
    TK_COLON,                  /* : */
    TK_SEMICOLON,              /* ; */
    TK_ELLIPSIS,               /* ... */
    TK_ASSIGN,                 /* = */
    TK_MUL_ASSIGN,             /* *= */
    TK_DIV_ASSIGN,             /* /= */
    TK_MOD_ASSIGN,             /* %= */
    TK_ADD_ASSIGN,             /* += */
    TK_SUB_ASSIGN,             /* -= */
    TK_LSHIFT_ASSIGN,          /* <<= */
    TK_RSHIFT_ASSIGN,          /* >>= */
    TK_AND_ASSIGN,             /* &= */
    TK_XOR_ASSIGN,             /* ^= */
    TK_OR_ASSIGN,              /* |= */
    TK_COMMA,                  /* , */
    TK_HASH,                   /* # (preprocessor) */
    TK_HASHHASH,               /* ## (token paste) */

    TK_NUM_KINDS
} TokenKind;

/* Integer literal suffix flags */
#define LIT_UNSIGNED  0x01
#define LIT_LONG      0x02
#define LIT_LONGLONG  0x04

typedef struct {
    TokenKind kind;
    SrcLoc    loc;
    const char *str;     /* interned string value for ident/string/char/number */
    int        str_len;  /* length of raw token text */

    /* For numeric literals */
    union {
        unsigned long long ival;
        double             fval;
    } num;
    int lit_suffix;      /* LIT_UNSIGNED, LIT_LONG, etc. */
    bool is_wide;        /* L"..." or L'...' */
    bool at_bol;         /* at beginning of line (for preprocessor) */
    bool has_space;      /* preceded by whitespace */
} Token;

/* Lexer state */
typedef struct {
    const char *src;       /* source text */
    const char *p;         /* current position */
    const char *filename;
    int line;
    int col;
    int prev_col;
    Token cur;             /* current token (after lexer_next) */
    Token peek;            /* one-token lookahead */
    bool has_peek;
    bool at_bol;           /* tracking beginning-of-line state */
} Lexer;

void  lexer_init(Lexer *l, const char *src, const char *filename);
void  lexer_next(Lexer *l);
Token lexer_peek(Lexer *l);
bool  lexer_match(Lexer *l, TokenKind kind);
void  lexer_expect(Lexer *l, TokenKind kind);

const char *token_kind_str(TokenKind kind);

/* Check if a token is a type specifier/qualifier keyword */
bool token_is_type_keyword(TokenKind kind);

#endif /* C99JS_LEXER_H */
