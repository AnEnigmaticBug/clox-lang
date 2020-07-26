#ifndef CLOX_SCANNER_H
#define CLOX_SCANNER_H

typedef enum
{
    TK_LEFT_PAREN,
    TK_RIGHT_PAREN,
    TK_LEFT_BRACE,
    TK_RIGHT_BRACE,
    TK_COMMA,
    TK_DOT,
    TK_MINUS,
    TK_PLUS,
    TK_SEMICOLON,
    TK_SLASH,
    TK_STAR,

    // One or two character tokens.
    TK_BANG,
    TK_BANG_EQUAL,
    TK_EQUAL,
    TK_EQUAL_EQUAL,
    TK_GREATER,
    TK_GREATER_EQUAL,
    TK_LESS,
    TK_LESS_EQUAL,

    // Literals.
    TK_IDENTIFIER,
    TK_STRING,
    TK_NUMBER,

    // Keywords.
    TK_AND,
    TK_CLASS,
    TK_ELSE,
    TK_FALSE,
    TK_FOR,
    TK_FUN,
    TK_IF,
    TK_NIL,
    TK_OR,
    TK_PRINT,
    TK_RETURN,
    TK_SUPER,
    TK_THIS,
    TK_TRUE,
    TK_VAR,
    TK_WHILE,

    TK_ERROR,
    TK_EOF
} TokenType;

typedef struct
{
    TokenType type;
    const char *start;
    int length;
    int line_no;
} Token;

void init_scanner(const char *source);
Token scan_token();

#endif
