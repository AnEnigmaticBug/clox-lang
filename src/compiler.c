#include "compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory.h"
#include "scanner.h"
#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct
{
    Token curr;
    Token prev;
    bool had_error;
    bool in_panic_mode;
} Parser;

typedef enum
{
    PREC_NONE,
    PREC_ASSIGNMENT, // =
    PREC_OR,         // or
    PREC_AND,        // and
    PREC_EQUALITY,   // == !=
    PREC_COMPARISON, // < > <= >=
    PREC_TERM,       // + -
    PREC_FACTOR,     // * /
    PREC_UNARY,      // ! -
    PREC_CALL,       // . ()
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool can_assign);

typedef struct
{
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct
{
    Token name;
    int depth;
    bool is_captured;
} Local;

typedef struct
{
    uint8_t index;
    bool is_local;
} Upvalue;

typedef enum
{
    TYPE_FUNCTION,
    TYPE_SCRIPT,
} FunctionType;

typedef struct Compiler
{
    struct Compiler *enclosing;
    ObjFunction *function;
    FunctionType type;
    Local locals[UINT8_COUNT];
    int local_count;
    Upvalue upvalues[UINT8_COUNT];
    int scope_depth;
} Compiler;

Parser parser;
Compiler *current = NULL;

static Chunk *curr_chunk()
{
    return &current->function->chunk;
}

static void error_at(Token *token, const char *message)
{
    if (parser.in_panic_mode)
    {
        return;
    }

    parser.in_panic_mode = true;

    fprintf(stderr, "[line %d] Error", token->line_no);

    if (token->type == TK_EOF)
    {
        fprintf(stderr, " at end");
    }
    else if (token->type != TK_ERROR)
    {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.had_error = true;
}

static void error_at_prev(const char *message)
{
    error_at(&parser.prev, message);
}

static void error_at_curr(const char *message)
{
    error_at(&parser.curr, message);
}

static void advance()
{
    parser.prev = parser.curr;
    parser.curr = scan_token();

    while (parser.curr.type == TK_ERROR)
    {
        parser.curr = scan_token();
        error_at_curr(parser.curr.start);
    }
}

static void consume(TokenType type, const char *message)
{
    if (parser.curr.type == type)
    {
        advance();
        return;
    }

    error_at_curr(message);
}

static bool check(TokenType type)
{
    return parser.curr.type == type;
}

static bool match(TokenType type)
{
    if (!check(type))
    {
        return false;
    }

    advance();
    return true;
}

static void emit_1_byte(uint8_t byte)
{
    append_to_chunk(curr_chunk(), byte, parser.prev.line_no);
}

static void emit_2_byte(uint8_t byte1, uint8_t byte2)
{
    emit_1_byte(byte1);
    emit_1_byte(byte2);
}

static void emit_loop(int loop_start)
{
    emit_1_byte(OP_LOOP);

    int offset = curr_chunk()->count - loop_start + 2;
    if (offset > UINT16_MAX)
    {
        error_at_curr("Loop body too large.");
    }

    emit_1_byte((offset >> 8) & 0xff);
    emit_1_byte(offset & 0xff);
}

static int emit_jump(uint8_t instr)
{
    emit_1_byte(instr);
    emit_1_byte(0xff);
    emit_1_byte(0xff);

    return curr_chunk()->count - 2;
}

static uint8_t make_constant(Value value)
{
    int constant = add_constant(curr_chunk(), value);
    if (constant > UINT8_MAX)
    {
        error_at_prev("Too many constants in chunk.");
        return 0;
    }

    return (uint8_t)constant;
}

static void emit_constant(Value value)
{
    emit_2_byte(OP_CONSTANT, make_constant(value));
}

static void patch_jump(int offset)
{
    int jump = curr_chunk()->count - offset - 2;

    if (jump > UINT16_MAX)
    {
        error_at_curr("Too much code to jump over.");
    }

    curr_chunk()->code[offset + 0] = (jump >> 8) & 0xff;
    curr_chunk()->code[offset + 1] = jump & 0xff;
}

static void init_compiler(Compiler *compiler, FunctionType type)
{
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->function = new_function();
    current = compiler;

    if (type != TYPE_SCRIPT)
    {
        current->function->name = copy_string(parser.prev.start, parser.prev.length);
    }

    Local *local = &current->locals[current->local_count++];
    local->depth = 0;
    local->is_captured = false;
    local->name.start = "";
    local->name.length = 0;
}

static ObjFunction *end_compiler()
{
    emit_2_byte(OP_NIL, OP_RETURN);
    ObjFunction *function = current->function;

#ifdef DEBUG_PRINT_CODE
    if (!parser.had_error)
    {
        disassemble_chunk(curr_chunk(), function->name != NULL ? function->name->chars : "<script>");
    }
#endif

    current = current->enclosing;
    return function;
}

static void begin_scope()
{
    ++current->scope_depth;
}

static void end_scope()
{
    --current->scope_depth;

    while (current->local_count > 0 &&
           current->locals[current->local_count - 1].depth > current->scope_depth)
    {
        if (current->locals[current->local_count - 1].is_captured)
        {
            emit_1_byte(OP_CLOSE_UPVALUE);
        }
        else
        {
            emit_1_byte(OP_POP);
        }
        --current->local_count;
    }
}

static void expression();
static void statement();
static void declaration();
static uint8_t argument_list();
static void and (bool can_assign);
static ParseRule *get_rule(TokenType type);
static void parse_precedence(Precedence precedence);
static uint8_t identifier_constant(Token *name);
static int resolve_local(Compiler *compiler, Token *name);
static int resolve_upvalue(Compiler *compiler, Token *name);

static void binary(bool can_assign)
{
    TokenType operator_type = parser.prev.type;

    ParseRule *rule = get_rule(operator_type);
    parse_precedence((Precedence)(rule->precedence + 1));

    switch (operator_type)
    {
    case TK_BANG_EQUAL:
        emit_2_byte(OP_EQUAL, OP_NOT);
        break;
    case TK_EQUAL_EQUAL:
        emit_1_byte(OP_EQUAL);
        break;
    case TK_GREATER:
        emit_1_byte(OP_GREATER);
        break;
    case TK_GREATER_EQUAL:
        emit_2_byte(OP_LESS, OP_NOT);
        break;
    case TK_LESS:
        emit_1_byte(OP_LESS);
        break;
    case TK_LESS_EQUAL:
        emit_2_byte(OP_GREATER, OP_NOT);
        break;
    case TK_PLUS:
        emit_1_byte(OP_ADD);
        break;
    case TK_MINUS:
        emit_1_byte(OP_SUB);
        break;
    case TK_STAR:
        emit_1_byte(OP_MUL);
        break;
    case TK_SLASH:
        emit_1_byte(OP_DIV);
        break;
    default:
        return;
    }
}

static void call(bool can_assign)
{
    uint8_t arg_count = argument_list();
    emit_2_byte(OP_CALL, arg_count);
}

static void dot(bool can_assign)
{
    consume(TK_IDENTIFIER, "Expect property name after '.'.");
    uint8_t name = identifier_constant(&parser.prev);

    if (can_assign && match(TK_EQUAL))
    {
        expression();
        emit_2_byte(OP_SET_PROPERTY, name);
    }
    else
    {
        emit_2_byte(OP_GET_PROPERTY, name);
    }
    
}

static void literal(bool can_assign)
{
    switch (parser.prev.type)
    {
    case TK_FALSE:
        emit_1_byte(OP_FALSE);
        break;
    case TK_NIL:
        emit_1_byte(OP_NIL);
        break;
    case TK_TRUE:
        emit_1_byte(OP_TRUE);
        break;
    default:
        return;
    }
}

static void grouping(bool can_assign)
{
    expression();
    consume(TK_RIGHT_PAREN, "Expect ) after expression.");
}

static void number(bool can_assign)
{
    double value = strtod(parser.prev.start, NULL);
    emit_constant(NUMBER_VAL(value));
}

static void or (bool can_assign)
{
    int else_jump = emit_jump(OP_JUMP_IF_FALSE);
    int end_jump = emit_jump(OP_JUMP);

    patch_jump(else_jump);
    emit_1_byte(OP_POP);

    parse_precedence(PREC_OR);
    patch_jump(end_jump);
}

static void string(bool can_assign)
{
    emit_constant(OBJ_VAL(copy_string(parser.prev.start + 1, parser.prev.length - 2)));
}

static void named_variable(Token name, bool can_assign)
{
    uint8_t get_op, set_op;
    int arg = resolve_local(current, &name);

    if (arg != -1)
    {
        get_op = OP_GET_LOCAL;
        set_op = OP_SET_LOCAL;
    }
    else if ((arg = resolve_upvalue(current, &name)) != -1)
    {
        get_op = OP_GET_UPVALUE;
        set_op = OP_SET_UPVALUE;
    }
    else
    {
        arg = identifier_constant(&name);
        get_op = OP_GET_GLOBAL;
        set_op = OP_SET_GLOBAL;
    }

    if (can_assign && match(TK_EQUAL))
    {
        expression();
        emit_2_byte(set_op, arg);
    }
    else
    {
        emit_2_byte(get_op, arg);
    }
}

static void variable(bool can_assign)
{
    named_variable(parser.prev, can_assign);
}

static void unary(bool can_assign)
{
    TokenType operator_type = parser.prev.type;

    parse_precedence(PREC_UNARY);

    switch (operator_type)
    {
    case TK_BANG:
        emit_1_byte(OP_NOT);
        break;
    case TK_MINUS:
        emit_1_byte(OP_NEGATE);
        break;
    default:
        return;
    }
}

ParseRule rules[] = {
    {grouping, call, PREC_CALL},     // TK_LEFT_PAREN
    {NULL, NULL, PREC_NONE},         // TK_RIGHT_PAREN
    {NULL, NULL, PREC_NONE},         // TK_LEFT_BRACE
    {NULL, NULL, PREC_NONE},         // TK_RIGHT_BRACE
    {NULL, NULL, PREC_NONE},         // TK_COMMA
    {NULL, dot, PREC_CALL},          // TK_DOT
    {unary, binary, PREC_TERM},      // TK_MINUS
    {NULL, binary, PREC_TERM},       // TK_PLUS
    {NULL, NULL, PREC_NONE},         // TK_SEMICOLON
    {NULL, binary, PREC_FACTOR},     // TK_SLASH
    {NULL, binary, PREC_FACTOR},     // TK_STAR
    {unary, NULL, PREC_NONE},        // TK_BANG
    {NULL, binary, PREC_EQUALITY},   // TK_BANG_EQUAL
    {NULL, NULL, PREC_NONE},         // TK_EQUAL
    {NULL, binary, PREC_EQUALITY},   // TK_EQUAL_EQUAL
    {NULL, binary, PREC_COMPARISON}, // TK_GREATER
    {NULL, binary, PREC_COMPARISON}, // TK_GREATER_EQUAL
    {NULL, binary, PREC_COMPARISON}, // TK_LESS
    {NULL, binary, PREC_COMPARISON}, // TK_LESS_EQUAL
    {variable, NULL, PREC_NONE},     // TK_IDENTIFIER
    {string, NULL, PREC_NONE},       // TK_STRING
    {number, NULL, PREC_NONE},       // TK_NUMBER
    {NULL, and, PREC_AND},           // TK_AND
    {NULL, NULL, PREC_NONE},         // TK_CLASS
    {NULL, NULL, PREC_NONE},         // TK_ELSE
    {literal, NULL, PREC_NONE},      // TK_FALSE
    {NULL, NULL, PREC_NONE},         // TK_FOR
    {NULL, NULL, PREC_NONE},         // TK_FUN
    {NULL, NULL, PREC_NONE},         // TK_IF
    {literal, NULL, PREC_NONE},      // TK_NIL
    {NULL, or, PREC_OR},             // TK_OR
    {NULL, NULL, PREC_NONE},         // TK_PRINT
    {NULL, NULL, PREC_NONE},         // TK_RETURN
    {NULL, NULL, PREC_NONE},         // TK_SUPER
    {NULL, NULL, PREC_NONE},         // TK_THIS
    {literal, NULL, PREC_NONE},      // TK_TRUE
    {NULL, NULL, PREC_NONE},         // TK_VAR
    {NULL, NULL, PREC_NONE},         // TK_WHILE
    {NULL, NULL, PREC_NONE},         // TK_ERROR
    {NULL, NULL, PREC_NONE},         // TK_EOF
};

static void parse_precedence(Precedence precedence)
{
    advance();
    ParseFn prefix_rule = get_rule(parser.prev.type)->prefix;
    if (prefix_rule == NULL)
    {
        error_at_prev("Expected expression.");
        return;
    }

    bool can_assign = precedence <= PREC_ASSIGNMENT;
    prefix_rule(can_assign);

    while (precedence <= get_rule(parser.curr.type)->precedence)
    {
        advance();
        ParseFn infix_rule = get_rule(parser.prev.type)->infix;
        infix_rule(can_assign);
    }

    if (can_assign && match(TK_EQUAL))
    {
        error_at_curr("Invalid assignment target.");
    }
}

static uint8_t identifier_constant(Token *name)
{
    return make_constant(OBJ_VAL(copy_string(name->start, name->length)));
}

static bool identifiers_equal(Token *a, Token *b)
{
    if (a->length != b->length)
    {
        return false;
    }

    return memcmp(a->start, b->start, a->length) == 0;
}

static int resolve_local(Compiler *compiler, Token *name)
{
    for (int i = compiler->local_count - 1; i >= 0; --i)
    {
        Local *local = &compiler->locals[i];
        if (identifiers_equal(name, &local->name))
        {
            if (local->depth == -1)
            {
                error_at_curr("Cannot read variable in its own initializer.");
            }
            return i;
        }
    }

    return -1;
}

static int add_upvalue(Compiler *compiler, uint8_t index, bool is_local)
{
    int upvalue_count = compiler->function->upvalue_count;

    for (int i = 0; i < upvalue_count; ++i)
    {
        Upvalue *upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->is_local == is_local)
        {
            return i;
        }
    }

    if (upvalue_count == UINT8_COUNT)
    {
        error_at_curr("Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalue_count].is_local = is_local;
    compiler->upvalues[upvalue_count].index = index;
    return compiler->function->upvalue_count++;
}

static int resolve_upvalue(Compiler *compiler, Token *name)
{
    if (compiler->enclosing == NULL)
    {
        return -1;
    }

    int local = resolve_local(compiler->enclosing, name);
    if (local != -1)
    {
        compiler->enclosing->locals[local].is_captured = true;
        return add_upvalue(compiler, (uint8_t)local, true);
    }

    int upvalue = resolve_upvalue(compiler->enclosing, name);
    if (upvalue != -1)
    {
        return add_upvalue(compiler, (uint8_t)upvalue, false);
    }

    return -1;
}

static void add_local(Token name)
{
    if (current->local_count == UINT8_COUNT)
    {
        error_at_curr("Too many local variables in function.");
        return;
    }

    Local *local = &current->locals[current->local_count++];
    local->name = name;
    local->depth = -1;
    local->is_captured = false;
}

static void declare_variable()
{
    if (current->scope_depth == 0)
    {
        return;
    }

    Token *name = &parser.prev;

    for (int i = current->local_count - 1; i >= 0; --i)
    {
        Local *local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scope_depth)
        {
            break;
        }

        if (identifiers_equal(name, &local->name))
        {
            error_at_curr("Variable with this name already declared in this scope.");
        }
    }

    add_local(*name);
}

static uint8_t parse_variable(const char *error_message)
{
    consume(TK_IDENTIFIER, error_message);

    declare_variable();

    if (current->scope_depth > 0)
    {
        return 0;
    }

    return identifier_constant(&parser.prev);
}

static void mark_initialized()
{
    if (current->scope_depth == 0)
    {
        return;
    }
    current->locals[current->local_count - 1].depth = current->scope_depth;
}

static void define_variable(uint8_t global)
{
    if (current->scope_depth > 0)
    {
        mark_initialized();
        return;
    }

    emit_2_byte(OP_DEFINE_GLOBAL, global);
}

static uint8_t argument_list()
{
    uint8_t arg_count = 0;
    if (!check(TK_RIGHT_PAREN))
    {
        do
        {
            expression();

            if (arg_count == 255)
            {
                error_at_curr("Cannot have more than 255 arguments.");
            }

            ++arg_count;
        } while (match(TK_COMMA));
    }

    consume(TK_RIGHT_PAREN, "Expect ')' after arguments.");
    return arg_count;
}

static void and (bool can_assign)
{
    int end_jump = emit_jump(OP_JUMP_IF_FALSE);

    emit_1_byte(OP_POP);
    parse_precedence(PREC_AND);

    patch_jump(end_jump);
}

static ParseRule *get_rule(TokenType type)
{
    return &rules[type];
}

static void expression()
{
    parse_precedence(PREC_ASSIGNMENT);
}

static void block()
{
    while (!check(TK_RIGHT_BRACE) && !check(TK_EOF))
    {
        declaration();
    }

    consume(TK_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(FunctionType type)
{
    Compiler compiler;
    init_compiler(&compiler, type);
    begin_scope();

    consume(TK_LEFT_PAREN, "Expect '(' after function name.");

    if (!check(TK_RIGHT_PAREN))
    {
        do
        {
            ++current->function->arity;
            if (current->function->arity > 255)
            {
                error_at_curr("Cannot have more than 255 parameters.");
            }

            uint8_t param_constant = parse_variable("Expect parameter name.");
            define_variable(param_constant);
        } while (match(TK_COMMA));
    }

    consume(TK_RIGHT_PAREN, "Expect ')' after parameters.");

    consume(TK_LEFT_BRACE, "Expect '{' before function body.");
    block();

    ObjFunction *function = end_compiler();
    emit_2_byte(OP_CLOSURE, make_constant(OBJ_VAL(function)));

    for (int i = 0; i < function->upvalue_count; ++i)
    {
        emit_1_byte(compiler.upvalues[i].is_local ? 1 : 0);
        emit_1_byte(compiler.upvalues[i].index);
    }
}

static void class_declaration()
{
    consume(TK_IDENTIFIER, "Expect class name.");
    uint8_t name_constant = identifier_constant(&parser.prev);
    declare_variable();

    emit_2_byte(OP_CLASS, name_constant);
    define_variable(name_constant);

    consume(TK_LEFT_BRACE, "Expect '{' before class body.");
    consume(TK_RIGHT_BRACE, "Expect '}' after class body.");
}

static void fun_declaration()
{
    uint8_t global = parse_variable("Expect function name.");
    mark_initialized();
    function(TYPE_FUNCTION);
    define_variable(global);
}

static void var_declaration()
{
    uint8_t global = parse_variable("Expect variable name.");

    if (match(TK_EQUAL))
    {
        expression();
    }
    else
    {
        emit_1_byte(OP_NIL);
    }

    consume(TK_SEMICOLON, "Expect ';' after variable declaration.");

    define_variable(global);
}

static void expression_statement()
{
    expression();
    consume(TK_SEMICOLON, "Expect ';' after expression.");
    emit_1_byte(OP_POP);
}

static void for_statement()
{
    begin_scope();

    consume(TK_LEFT_PAREN, "Expect '(' after 'for'.");

    if (match(TK_SEMICOLON))
    {
    }
    else if (match(TK_VAR))
    {
        var_declaration();
    }
    else
    {
        expression_statement();
    }

    int loop_start = curr_chunk()->count;

    int exit_jump = -1;
    if (!match(TK_SEMICOLON))
    {
        expression();
        consume(TK_SEMICOLON, "Expect ';' after loop condition.");

        exit_jump = emit_jump(OP_JUMP_IF_FALSE);
        emit_1_byte(OP_POP);
    }

    if (!match(TK_RIGHT_PAREN))
    {
        int body_jump = emit_jump(OP_JUMP);

        int increment_start = curr_chunk()->count;
        expression();
        emit_1_byte(OP_POP);
        consume(TK_RIGHT_PAREN, "Expect ')' after for clauses.");

        emit_loop(loop_start);
        loop_start = increment_start;
        patch_jump(body_jump);
    }

    statement();

    emit_loop(loop_start);

    if (exit_jump != -1)
    {
        patch_jump(exit_jump);
        emit_1_byte(OP_POP);
    }

    end_scope();
}

static void if_statement()
{
    consume(TK_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TK_RIGHT_PAREN, "Expect ')' after condition.");

    int then_jump = emit_jump(OP_JUMP_IF_FALSE);
    emit_1_byte(OP_POP);
    statement();

    int else_jump = emit_jump(OP_JUMP);

    patch_jump(then_jump);
    emit_1_byte(OP_POP);

    if (match(TK_ELSE))
    {
        statement();
    }

    patch_jump(else_jump);
}

static void print_statement()
{
    expression();
    consume(TK_SEMICOLON, "Expect ';' after value.");
    emit_1_byte(OP_PRINT);
}

static void return_statement()
{
    if (current->type == TYPE_SCRIPT)
    {
        error_at_curr("Cannot return from top-level code.");
    }

    if (match(TK_SEMICOLON))
    {
        emit_2_byte(OP_NIL, OP_RETURN);
        return;
    }

    expression();
    consume(TK_SEMICOLON, "Expect ';' after return value.");
    emit_1_byte(OP_RETURN);
}

static void while_statement()
{
    int loop_start = curr_chunk()->count;

    consume(TK_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TK_RIGHT_PAREN, "Expect ')' after condition.");

    int exit_jump = emit_jump(OP_JUMP_IF_FALSE);

    emit_1_byte(OP_POP);
    statement();

    emit_loop(loop_start);

    patch_jump(exit_jump);
    emit_1_byte(OP_POP);
}

static void synchronize()
{
    parser.in_panic_mode = false;

    while (parser.curr.type != TK_EOF)
    {
        if (parser.prev.type == TK_SEMICOLON)
        {
            return;
        }

        switch (parser.curr.type)
        {
        case TK_CLASS:
        case TK_FUN:
        case TK_VAR:
        case TK_FOR:
        case TK_IF:
        case TK_WHILE:
        case TK_PRINT:
        case TK_RETURN:
            return;
        default:;
        }

        advance();
    }
}

static void declaration()
{
    if (match(TK_CLASS))
    {
        class_declaration();
    }
    else if (match(TK_FUN))
    {
        fun_declaration();
    }
    else if (match(TK_VAR))
    {
        var_declaration();
    }
    else
    {
        statement();
    }

    if (parser.in_panic_mode)
    {
        synchronize();
    }
}

static void statement()
{
    if (match(TK_PRINT))
    {
        print_statement();
    }
    else if (match(TK_FOR))
    {
        for_statement();
    }
    else if (match(TK_IF))
    {
        if_statement();
    }
    else if (match(TK_RETURN))
    {
        return_statement();
    }
    else if (match(TK_WHILE))
    {
        while_statement();
    }
    else if (match(TK_LEFT_BRACE))
    {
        begin_scope();
        block();
        end_scope();
    }
    else
    {
        expression_statement();
    }
}

ObjFunction *compile(const char *source)
{
    init_scanner(source);

    Compiler compiler;
    init_compiler(&compiler, TYPE_SCRIPT);

    parser.had_error = false;
    parser.in_panic_mode = false;

    advance();

    while (!match(TK_EOF))
    {
        declaration();
    }

    ObjFunction *function = end_compiler();
    return parser.had_error ? NULL : function;
}

void mark_compiler_roots()
{
    Compiler *compiler = current;
    while (compiler != NULL)
    {
        mark_obj((Obj *)compiler->function);
        compiler = compiler->enclosing;
    }
}
