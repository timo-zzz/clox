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

Parser parser;
Chunk* compilingChunk;

// For user-defined function, the "current chunk" becomes a bit more nuanced. So, this will hold that logic.
static Chunk* currentChuck() {
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

static void endCompiler() {
    emitReturn();
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