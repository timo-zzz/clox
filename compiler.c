#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

// Since enums are just numbers, some enums are larger numerically than others. That is their precedence value.
typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_OR,          // or
    PREC_AND,         // and
    PREC_EQUALITY,    // == !=
    PREC_COMPARISON,  // < > <= >=
    PREC_TERM,        // + -
    PREC_FACTOR,      // * /
    PREC_UNARY,       // ! -
    PREC_CALL,        // . ()
    PREC_PRIMARY
} Precedence;

// Parsing function pointer type
typedef void (*ParseFn)();

typedef struct {
    ParseFn prefix;        // The function to compile the prefix expression this token is used for
    ParseFn infix;         // The function to compile the infix expression this token is used for
    Precedence precedence; // The precedence of the infix expression when using this token as an operator
} ParseRule; // Represents a row in the parser table (see line 178)

Parser parser;
Chunk* compilingChunk;

// For user-defined function, the "current chunk" becomes a bit more nuanced. So, this will hold that logic.
static Chunk* currentChunk() {
    return compilingChunk;
}

static void errorAt(Token* token, const char* message) {
    if (parser.panicMode) return; // If in panic mode, ignore errors until recovery point (will be added later)
    parser.panicMode = true;
    // Print to error stream the line of the error 
    fprintf(stderr, "[line %d] Error", token->line); // I lowkey love C syntax

    if (token->type == TOKEN_EOF) {
        // If at EOF (end of file), signify that
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Do nothing (errors found during scanning)
    } else {
        // Print which token the error is at
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    // Print error message
    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

// Reports an error at the token that was just consumed
static void error(const char* message) {
    errorAt(&parser.previous, message);
}

// Reports an error at the current token
static void errorAtCurrent(const char* message) {
    errorAt(&parser.current, message);
}

// "Advance" a token in parsing/compilation. Basically, move the current token back one, then move forward a token
static void advance() {
    parser.previous = parser.current; // Store the current token

    // Error check loop. Continues only if there is an error, so the parser only sees valid tokens
    for (;;) {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR) break; 

        errorAtCurrent(parser.current.start);
    }
}

// Like advance, but checks for the expected type. Main source of syntax errors.
static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return; // No need to fall through to error if its right
    }

    errorAtCurrent(message);
}

// Add a byte (opcode or operand) to the chunk. The previous token's line info is sent so that runtime errors are associated with that line.
static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

// When clox is run, it parses, compiles, and executes an expression, then prints it result. So, we temporarily use return to do that.
static void emitReturn() {
    emitByte(OP_RETURN);
}

// Adds a value to the end of current chunk's constant table/pool, and then returns its index
static uint8_t makeConstant(Value value) {
    int constantIndex = addConstant(currentChunk(), value);
    if (constantIndex > UINT8_MAX) {
        error("Too many constants in one chunk."); // Chunk of BYTEcode
        return 0;
    }

    return (uint8_t)constantIndex;
}

// Adds a constant to the constant table, pushes its index in the constant table onto the stack, then pushes a constant opcode onto the stack
static void emitConstant(Value value) {
    emitBytes(OP_CONSTANT, makeConstant(value));
}

static void endCompiler() {
    emitReturn();
#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {  // Only dump chunk if there was no errors
        disassembleChunk(currentChunk(), "code");
    }
#endif
}

// Forward declarations for use in grammar production methods
static void expression();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

// Compiles the right operand, then emits the operation opcode
static void binary() {
    // Handles operation precedence, so we can use 1 function for all binary operations
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1)); // +1 because binary operations associate left

    switch (operatorType) {
        case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL, OP_NOT); break;
        case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
        case TOKEN_GREATER:       emitByte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
        case TOKEN_LESS:          emitByte(OP_LESS); break;
        case TOKEN_LESS_EQUAL:    emitBytes(OP_GREATER, OP_NOT); break;
        case TOKEN_PLUS:          emitByte(OP_ADD); break;
        case TOKEN_MINUS:         emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;
    }
}

static void literal() {
    // Keyword token has already been consumed
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_NIL: emitByte(OP_NIL); break;
        case TOKEN_TRUE: emitByte(OP_TRUE); break;
        default: return; // Unreachable
    }
}

static void grouping() {
    expression();
    // Assumes the token has already been consumed
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");

    // Doesn't emit any bytecode because a grouping expression just changes precedence.
}

static void number() {
    // Assume the token has already been consumed (use the previous token)
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

static void unary() {
    // Assume the token has already been consumed (use the previous token)
    TokenType operatorType = parser.previous.type;

    // Compile/evaluate the operand. This is done first so negation is done correctly
    parsePrecedence(PREC_UNARY);

    // Emit the operator instruction. 
    switch (operatorType) {
        case TOKEN_BANG: emitByte(OP_NOT); break;
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        default: return; // Unreachable
    }
}
/*
  Each expression has a corresponding TokenType. Since enums are just numbers, each TokenType enum is an index in this table of function pointers.
  This information is stored in a ParseRule struct. So, using a token type, we can easily look up its compiling function.
*/
ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping, NULL,   PREC_NONE},
    [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
    [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
    [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
    [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
    [TOKEN_BANG]          = {unary,    NULL,   PREC_NONE},
    [TOKEN_BANG_EQUAL]    = {NULL,     binary, PREC_NONE},
    [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_NONE},
    [TOKEN_GREATER]       = {NULL,     binary, PREC_NONE},
    [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_NONE},
    [TOKEN_LESS]          = {NULL,     binary, PREC_NONE},
    [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_NONE},
    [TOKEN_IDENTIFIER]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_STRING]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
    [TOKEN_AND]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
    [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
    [TOKEN_OR]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_THIS]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_TRUE]          = {literal,  NULL,   PREC_NONE},
    [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

static void parsePrecedence(Precedence precedence) {
    advance();

    // Parse prefix expression (the current token is ALWAYS a prefix expression)
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error("Expect expression.");
        return;
    }

    prefixRule();

    // Parse infix expressions (if precedence parameter permits)
    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule();
    }
}

// Look up a ParseRule using a TokenType. This is necessary because binary() recursively accesses the table (which stores binary in a rule)
static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

static void expression() {
    parsePrecedence(PREC_ASSIGNMENT);
}

bool compile(const char* source, Chunk* chunk) {
    // Initilization
    initScanner(source);
    compilingChunk = chunk;


    parser.hadError = false;
    parser.panicMode = false;

    advance();
    expression(); // Latuh
    consume(TOKEN_EOF, "Expect end of expression"); // Expect end of file
    endCompiler(); // Adds OP_RETURN to the end of the chunk
    return !parser.hadError; // Returns whether or not compilation suceeded (false if theres an error)
}