#include "scanner.h"
#include <string.h>
#include "common.h"

typedef struct
{
    const char *start;
    const char *current;
    int line_no;
} Scanner;

Scanner scanner;

void init_scanner(const char *source)
{
    scanner.start = source;
    scanner.current = source;
    scanner.line_no = 1;
}

static bool is_alpha(char c)
{
    return ('A' <= c && c <= 'Z') ||
           ('a' <= c && c <= 'z') ||
           c == '_';
}

static bool is_digit(char c)
{
    return '0' <= c && c <= '9';
}

static bool is_at_end()
{
    return *scanner.current == '\0';
}

static char advance()
{
    ++scanner.current;
    return scanner.current[-1];
}

static char peek()
{
    return *scanner.current;
}

static char peek_next()
{
    if (is_at_end())
    {
        return '\0';
    }
    return scanner.current[1];
}

static bool match(char expected)
{
    if (is_at_end() || *scanner.current != expected)
    {
        return false;
    }

    scanner.current++;
    return true;
}

static Token make_token(TokenType type)
{
    Token token;
    token.type = type;
    token.start = scanner.start;
    token.length = (int)(scanner.current - scanner.start);
    token.line_no = scanner.line_no;
    return token;
}

static Token error_token(const char *message)
{
    Token token;
    token.type = TK_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line_no = scanner.line_no;
    return token;
}

static void skip_whitespace()
{
    while (true)
    {
        char c = peek();
        switch (c)
        {
        case ' ':
        case '\r':
        case '\t':
        {
            advance();
            break;
        }
        case '\n':
        {
            ++scanner.line_no;
            advance();
            break;
        }
        case '/':
        {
            if (peek_next() == '/')
            {
                while (peek() != '\n' && !is_at_end())
                {
                    advance();
                }
            }
            else
            {
                return;
            }

            break;
        }
        default:
        {
            return;
        }
        }
    }
}

static TokenType check_keyword(int start, int length, const char *rest, TokenType type)
{
    if (scanner.current - scanner.start == start + length &&
        memcmp(scanner.start + start, rest, length) == 0)
    {
        return type;
    }

    return TK_IDENTIFIER;
}

static TokenType identifier_type()
{
    switch (scanner.start[0])
    {
    case 'a':
        return check_keyword(1, 2, "nd", TK_AND);
    case 'c':
        return check_keyword(1, 4, "lass", TK_CLASS);
    case 'e':
        return check_keyword(1, 3, "lse", TK_ELSE);
    case 'f':
        if (scanner.current - scanner.start > 1)
        {
            switch (scanner.start[1])
            {
            case 'a':
                return check_keyword(2, 3, "lse", TK_FALSE);
            case 'o':
                return check_keyword(2, 1, "r", TK_FOR);
            case 'u':
                return check_keyword(2, 1, "n", TK_FUN);
            }
        }
        break;
    case 'i':
        return check_keyword(1, 1, "f", TK_IF);
    case 'n':
        return check_keyword(1, 2, "il", TK_NIL);
    case 'o':
        return check_keyword(1, 1, "r", TK_OR);
    case 'p':
        return check_keyword(1, 4, "rint", TK_PRINT);
    case 'r':
        return check_keyword(1, 5, "eturn", TK_RETURN);
    case 's':
        return check_keyword(1, 4, "uper", TK_SUPER);
    case 't':
        if (scanner.current - scanner.start > 1)
        {
            switch (scanner.start[1])
            {
            case 'h':
                return check_keyword(2, 2, "is", TK_THIS);
            case 'r':
                return check_keyword(2, 2, "ue", TK_TRUE);
            }
        }
        break;
    case 'v':
        return check_keyword(1, 2, "ar", TK_VAR);
    case 'w':
        return check_keyword(1, 4, "hile", TK_WHILE);
    }
    return TK_IDENTIFIER;
}

static Token identifier()
{
    while (is_alpha(peek()) || is_digit(peek()))
    {
        advance();
    }

    return make_token(identifier_type());
}

static Token number()
{
    while (is_digit(peek()))
    {
        advance();
    }

    if (peek() == '.' && is_digit(peek_next()))
    {
        advance();
        while (is_digit(peek()))
        {
            advance();
        }
    }

    return make_token(TK_NUMBER);
}

static Token string()
{
    while (peek() != '"' && !is_at_end())
    {
        if (peek() == '\n')
        {
            ++scanner.line_no;
        }
        advance();
    }

    if (is_at_end())
    {
        return error_token("Unterminated string.");
    }

    advance();
    return make_token(TK_STRING);
}

Token scan_token()
{
    skip_whitespace();

    scanner.start = scanner.current;

    if (is_at_end())
    {
        return make_token(TK_EOF);
    }

    char c = advance();

    if (is_alpha(c))
    {
        return identifier();
    }

    if (is_digit(c))
    {
        return number();
    }

    switch (c)
    {
    case '(':
        return make_token(TK_LEFT_PAREN);
    case ')':
        return make_token(TK_RIGHT_PAREN);
    case '{':
        return make_token(TK_LEFT_BRACE);
    case '}':
        return make_token(TK_RIGHT_BRACE);
    case ';':
        return make_token(TK_SEMICOLON);
    case ',':
        return make_token(TK_COMMA);
    case '.':
        return make_token(TK_DOT);
    case '-':
        return make_token(TK_MINUS);
    case '+':
        return make_token(TK_PLUS);
    case '/':
        return make_token(TK_SLASH);
    case '*':
        return make_token(TK_STAR);
    case '!':
        return make_token(match('=') ? TK_BANG_EQUAL : TK_BANG);
    case '=':
        return make_token(match('=') ? TK_EQUAL_EQUAL : TK_EQUAL);
    case '<':
        return make_token(match('=') ? TK_LESS_EQUAL : TK_LESS);
    case '>':
        return make_token(match('=') ? TK_GREATER_EQUAL : TK_GREATER);
    case '"':
        return string();
    }

    return error_token("Unexpected character.");
}
