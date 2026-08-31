#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
typedef void (*ParseFn)(bool canAssign);

// Represents a row in the parser table (see line 178).
typedef struct {
    ParseFn prefix;        // The function to compile the prefix expression this token is used for
    ParseFn infix;         // The function to compile the infix expression this token is used for
    Precedence precedence; // The precedence of the infix expression when using this token as an operator
} ParseRule; 

// Represents a local variable.
typedef struct {
    Token name; // Variable's name
    int depth;  // The number of blocks/closures surrounding this variable
} Local;

// Tells the compiler when it is compiling inside of a function or not (so top level code). See first comment in the Compiler struct.
typedef enum {
    TYPE_FUNCTION,
    TYPE_SCRIPT // Did you know the comma at the end of the last enum type is optional? It doesn't make a difference. I just learned that. 
} FunctionType;

// Used to track the function the compiler is currently running and the state of all local variables.
typedef struct {
    // When we are not compiling to a chunk inside of a user-defined function (so in the global scope, or top level code), it is compiling to a "default" function. Our code is sort of wrapped in an implicit main function.
    ObjFunction* function; // Tracks the function we are currently compiling to
    FunctionType type;     // Tracks the type (top-level or real function) that we are currently compiling

    Local locals[UINT8_COUNT]; // Array of all local variables in every part of the compilation process. Ordered in the order they appear in code
    int localCount;            // How many locals are in scope (how many array slots are in use)
    int scopeDepth;            // The number of blocks/closures surrounding the code we're currently compiling
} Compiler;

Parser parser;
Compiler* current = NULL; // hi
Chunk* compilingChunk;

// For user-defined functions, the "current chunk" becomes a bit more nuanced. So, this will hold that logic.
static Chunk* currentChunk() {
    return &current->function->chunk; // I just realized arrow operator has higher precedence than ampersand.
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

// Checks if the current token in the parser is a given type.
static bool check(TokenType type) {
    return parser.current.type == type;
}

// Advances if the current token is a given type. Returns true/false if it was the given type
static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

// Add a byte (opcode or operand) to the chunk. The previous token's line info is sent so that runtime errors are associated with that line.
static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

static void emitLoop(int loopStart) {
    emitByte(OP_LOOP); // Jumps backwards       

    // Calculate how far back we need to jump (+2 bc we need to jump over OP_LOOP's operands too)
    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large");

    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

// Emits a bytecode instruction with a placeholder operand for its jump offset. 
static int emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 2;
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

// Uses the evaluated bytecode to calculate how far a jump is and then changes the jump instruction's operand (call after statement to be jumped over is evaluated)
static void patchJump(int offset) {
    // Calculate how far the jump is. -2 to adjust for the bytecode for the jump operand offset itself.
    int jump = currentChunk()->count - offset - 2;

    // If jump is too large, error
    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }

    // Change jump instruction operand (remember arrays are 0 indexed so this is 1 after the actual instruction)
    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

static void initCompiler(Compiler* compiler, FunctionType type) {
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->function = newFunction(); // We set it to null then initialize it a little bit later to prepare for our garbage collector later :)
    current = compiler;

    // Compiler reserves the 1st local variable slot for its own use.
    Local* local = &current->locals[current->localCount++];
    local->depth = 0;
    // Note that "name" is never actually initialized, we are just modifying its fields. So it is still NULL.
    local->name.start = ""; // Empty name so that the user can't create an identifier that refers to it.
    local->name.length = 0;
}

static ObjFunction* endCompiler() {
    emitReturn();
    ObjFunction* function = current->function;
    // Dump disassembled bytecode for debugging
#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {  // Only dump chunk if there was no errors
        disassembleChunk(currentChunk(), function->name != NULL
            ? function->name->chars : "<script>"); // Check if its an actual function, and dump its name accordingly. "<script>" is for if it is top-level/global code.
    }
#endif

    return function;
}

static void beginScope() {
    current->scopeDepth++;
}

static void endScope() {
    current->scopeDepth--;

    // Pop local variables after exiting a scope. localCount > 0 ensures everything gets popped in an outermost scope (its an AND so it doesnt apply outside outermost scopes)
    while (current->localCount > 0 && current->locals[current->localCount - 1].depth > current->scopeDepth) {
        emitByte(OP_POP);
        current->localCount--;
    }
}

// Forward declarations for use in grammar production methods
static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);
static uint8_t identifierConstant(Token* name);

static int resolveLocal(Compiler* compiler, Token* name);

// Compiles the right operand, then emits the operation opcode
static void binary(bool canAssign) {
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

static void literal(bool canAssign) {
    // Keyword token has already been consumed
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_NIL: emitByte(OP_NIL); break;
        case TOKEN_TRUE: emitByte(OP_TRUE); break;
        default: return; // Unreachable
    }
}

static void grouping(bool canAssign) {
    expression();
    // Assumes the token has already been consumed
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");

    // Doesn't emit any bytecode because a grouping expression just changes precedence.
}

// Wraps a number into a Value
static void number(bool canAssign) {
    // Assume the token has already been consumed (use the previous token)
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

static void or_(bool canAssign) {
    // Jumps over the endJump if the left operand is false
    int elseJump = emitJump(OP_JUMP_IF_FALSE);
    // Jumps over parsing the or if the left operand is true
    int endJump = emitJump(OP_JUMP);

    patchJump(elseJump);
    emitByte(OP_POP);

    parsePrecedence(PREC_OR);
    patchJump(endJump);
}

// Creates a String Obj, then wraps it in a Value
static void string(bool canAssign) {
    // +1 and -2 trim quotation marks
    emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

// Used to set & get variables
static void namedVariable(Token name, bool canAssign) {
    uint8_t getOp, setOp;
    int arg = resolveLocal(current, &name); // Needs to be a signed int since -1 represents global scope

    // Assign correct instructions for local and global variables respectively
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    } else {
        // "Add" the variable's name to the constant table (but really get its index since its an interned string (if it exists ofc))
        arg = identifierConstant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        // Check if an expression is an identifier being assigned a value
        expression();
        emitBytes(setOp, (uint8_t)arg); // Cast back to unsigned int
    } else {
        // Emit the instructions to access variable
        emitBytes(OP_GET_GLOBAL, (uint8_t)arg); // Cast back to unsigned int
    }
}

static void variable(bool canAssign) {
    namedVariable(parser.previous, canAssign);
}

static void unary(bool canAssign) {
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
 

static void and_(bool canAssign) {
    // Short circuit jump (and short circuits if first expression is false)
    int endJump = emitJump(OP_JUMP_IF_FALSE);

    // Pop condition expression
    emitByte(OP_POP);
    // Parse the 'and' expression
    parsePrecedence(PREC_AND);

    // Patch the jump
    patchJump(endJump);
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
    [TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_GREATER]       = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS]          = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER]    = {variable, NULL,   PREC_NONE},
    [TOKEN_STRING]        = {string,   NULL,   PREC_NONE},
    [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
    [TOKEN_AND]           = {NULL,     and_,   PREC_AND},
    [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
    [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
    [TOKEN_OR]            = {NULL,     or_,    PREC_OR},
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

    // Check if we can do assignment (will be false if nested in a higher-precedence expression)
    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    // Parse infix expressions (if precedence parameter permits)
    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }

    // If we have a leftover/unconsumed '=' token (from failed assignment due to precedence), throw an error
    if (canAssign && match(TOKEN_EQUAL)) {
        error("Invalid assignment target.");
    }
}

// Takes an identifier token's lexeme then adds it to the chunk's constant table as a string, then returns the index of that constant in the table.
static uint8_t identifierConstant(Token* name) {
    return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

// Checks if two identifiers are the same
static bool identifiersEqual(Token* a, Token* b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

// Tries to resolve an existing local variable
static int resolveLocal(Compiler* compiler, Token* name) {
    // Loop through the local variable array. Traverses backwards because new local variables are appended to the end (like a stack!)
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];

        // Check if theres a local variable with name
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer.");
            }
            // If so, return its index.
            return i;
        }
    }

    // If we never found it, its a global variable.
    return -1;
}

// Adds a new local variable to the array
static void addLocal(Token name) {
    // Don't allow adding if # of locals is at liit
    if (current->localCount == UINT8_COUNT) {
        error("Too many local variables in function.");
        return;
    }

    // Increment the amount of locals, then access the array of local variables using the amount of locals as the idx
    Local* local = &current->locals[current->localCount++];

    // Store the local
    local->name = name;
    local->depth = -1; // Represents an uninitialized state to prevent a variable from setting it to itself (e.x. var a = a;)
}

// Used to declare local variables
static void declareVariable() {
    // If its a global variable, exit.
    if (current->scopeDepth == 0) return;

    Token* name = &parser.previous;

    // Check if theres already a local variable in scope with the same name. Traverses backwards because new local variables are appended to the end (like a stack!)
    for (int i = current->localCount - 1; i >= 0; i--) {
        Local* local = &current->locals[i];
        // Stop the check if we've went outside the current scope 
        if (local->depth != -1 && local->depth < current->scopeDepth) {
            break;
        }

        // If so, report an error.
        if (identifiersEqual(name, &local->name)) {
            error("Already a variable with this name in this scope.");
        }
    }
    addLocal(*name);
}

// Parses a variable and returns the index of where it was placed in the chunk's constant table.
static uint8_t parseVariable(const char* errorMessage) {
    consume(TOKEN_IDENTIFIER, errorMessage);

    // how much wood would a woodchuck chuck if a woodchuck could chuck wood
    declareVariable();
    // Since local variables are handled entirely at compile time, we don't need to put an index in the table to look up at runtime.
    // So, we just exit the function instead.
    if (current->scopeDepth > 0) return 0;

    return identifierConstant(&parser.previous);
}

// Marks the last local declared as initialized
static void markInitialized() {
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}

// Defines a global variable by emitting its opcode and the index of the variable name onto the stack
static void defineVariable(uint8_t global) {
    // If it isn't a global variable, we don't need to do anything at runtime. The temporary data on the stack is the local variable itself.
    if (current->scopeDepth > 0) {
        markInitialized();
        return;
    }

    emitBytes(OP_DEFINE_GLOBAL, global);
}

// Look up a ParseRule using a TokenType. This is necessary because binary() recursively accesses the table (which stores binary in a rule)
static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

static void expression() {
    parsePrecedence(PREC_ASSIGNMENT);
}

static void block() {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }

    // If the program hits EOF and theres no closing curly, error (so we don't get stuck in a loop)
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

// Declares a variable
static void varDeclaration() {
    // Put the variable's name in the constant table & store its index
    uint8_t global = parseVariable("Expect variable name.");

    if (match(TOKEN_EQUAL)) {
        // If we find an equals sign after the variable name, evaluate the expression being assigned to the variable name
        expression();
    } else {
        // Assign nil as default value
        emitByte(OP_NIL);
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

    defineVariable(global);
}

// Evaluate an expression statement (i.e. a function call) (usually needs a side effect to do something)
static void expressionStatement() {
    // Evaluate the expression & put it on the stack
    expression();

    // Check for a semicolon & consume it
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");

    // Emit the instruction to pop the expression's value off the stack
    emitByte(OP_POP);
}

static void forStatement() {
    beginScope(); // Make sure initializer only exists in this loop
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
    // Initializer
    if (match(TOKEN_SEMICOLON)) {
        // No initializer.
    } else if (match(TOKEN_VAR)) {
        varDeclaration();
    } else {
        expressionStatement(); // Looks for semicolon and pops the value as opposed to expression();
    }

    // Condition
    int exitJump = -1;
    if (!match(TOKEN_SEMICOLON)) { // If next token isn't semicolon, theres a condition to be evaluated
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

        // Jump out of the loop if the condition is false
        exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP); // Pop the condition when its true
    }

    int loopStart = currentChunk()->count;
    consume(TOKEN_SEMICOLON, "Expect ';'.");

    // Increment
    if (!match(TOKEN_RIGHT_PAREN)) { // If the next token isn't right paren, theres an increment
        // Jump over the increment to the loop body
        int bodyJump = emitJump(OP_JUMP);
        int incrementStart = currentChunk()->count;
        expression(); // Compile increment expression
        emitByte(OP_POP); // Pop the value (since we only compile the expression for its side effect)
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

        emitLoop(loopStart); // Loop
        loopStart = incrementStart; // Change the start of the loop to where the increment happens, so we jump to the increment instead
        patchJump(bodyJump); // Patch the jump's operands
    }

    statement();
    emitLoop(loopStart);

    // Patch the jump if the condition exists
    if (exitJump != -1) {
        patchJump(exitJump);
        emitByte(OP_POP); // Pop the condition
    }

    endScope();
}

// Evaluate an if statement
static void ifStatement() {
    // Consume parentheses and evaluate expression
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    // Emit a jump instruction and store where it is
    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    // Pop condition expression
    emitByte(OP_POP);

    // Evaluate the statement in the if/then branch statement
    statement();

    // Emit a jump instruction inside the if/then branch statement. This way, if the then branch statement executes, we'll automatically jump over the else branch.
    int elseJump = emitJump(OP_JUMP);

    // Go back to the jump instruction and change its operand to the correct amount (patching)
    patchJump(thenJump);

    // Pop condition expression if we jumped over the other pop (so if the condition was false)
    emitByte(OP_POP);

    // If theres an else statement, evaluate it
    if (match(TOKEN_ELSE)) statement();
    // Patch the else statement
    patchJump(elseJump);
}

// Evaluate a print statement 
static void printStatement() {
    // Evaluate the expression & put it on the stack so that it can be popped & printed
    expression();

    // Check for a semicolon & consume it
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    
    // Emit the print instruction to the current chunk.
    emitByte(OP_PRINT);
}

static void whileStatement() {
    // Store where the loop starts
    int loopStart = currentChunk()->count;

    // Consume parentheses and evaluate expression
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    // Emit a jump instruction for exiting the loop if the condition is false
    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP); // Pop condition expression
    statement(); // Evaluate statement
    emitLoop(loopStart); // Continually jumps back to the start until the condition is false

    // Go back to the jump instruction and change its operand to the correct amount (patching)
    patchJump(exitJump);
    emitByte(OP_POP);

}

// Synchronize errors
static void synchronize() {
    parser.panicMode = false;

    while (parser.current.type != TOKEN_EOF) {
        // Exit panic mode at a statement boundary
        if (parser.previous.type == TOKEN_SEMICOLON) return;
        switch (parser.current.type) {
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
                return;

            default:
                ; // Do nothing, or rather...
        }

        // ...advance! Until we hit a statement boundary
        advance();
    }
}

static void declaration() {
    if (match(TOKEN_VAR)) {
        varDeclaration();
    } else {
        statement();
    }

    if (parser.panicMode) synchronize();
}

static void statement() {
    if (match(TOKEN_PRINT)) {
        printStatement();
    } else if (match(TOKEN_FOR)) {
        forStatement();
    } else if (match(TOKEN_IF)) {
        ifStatement();
    } else if (match(TOKEN_WHILE)) {
        whileStatement();
    } else if (match(TOKEN_LEFT_BRACE)) {
        beginScope();
        block();
        endScope();
    } else {
        expressionStatement();
    }
}

ObjFunction* compile(const char* source) {
    // Initilization
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);

    parser.hadError = false;
    parser.panicMode = false;

    advance();

    while (!match(TOKEN_EOF)) {
        declaration();
    }

    ObjFunction* function = endCompiler(); // endCompiler() adds OP_RETURN to the end of the chunk
    return parser.hadError ? NULL : function; // Return the function the VM will run (return NULL if theres errors)
}