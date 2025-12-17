#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"

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
        fprintf(stderr, "at '%.*s", token->length, token->start);
    }

    // Print error message
    fprintf(stderr, ": %s\n", message);
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

static void emitBytes(uint8_t byte1, uint8t byte2) {
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
    emitConstant(value);
}

static void unary() {
    // Assume the token has already been consumed (use the previous token)
    TokenType operatorType = parser.previous.type;

    // Compile/evaluate the operand. This is done first so negation is done correctly
    parsePrecedence(PREC_UNARY);

    // Emit the operator instruction. 
    switch (operatorType) {
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        default: return; // Unreachable
    }
}

static void parsePrecedence(Precedence precedence) {

}

/* 
   Each expression is represented by a token type, and gets its own function. Then, we create an array of pointers to these functions. The
   indexes in this array will correspond to the TokenType enum values (enums are just numbers with names!). Of course, the function at a
   TokenType's enum value index will compile bytecode for that expression.
*/
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