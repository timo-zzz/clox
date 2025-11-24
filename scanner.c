#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

typedef struct {
    const char* start;
    const char* current;
    int line;
} Scanner;

Scanner scanner;

void initScanner(const char* source) {
    scanner.start = source;
    scanner.current = source;
    scanner.line = 1;
}

static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
            c == '_'; // Identifiers have these things too
}

static bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

// Ts jlox reference so heat ❤️‍🩹❤️‍🩹
static bool isAtEnd() {
    // Quick review! '\0', or the null/string terminator character, always ends a string!
    return *scanner.current == '\0';
}

// Returns the current character, and then goes to the next one (consumes current one).
static char advance() {
    scanner.current++;
    return scanner.current[-1];
}

// Returns the current character without consuming it
static char peek() {
    return *scanner.current;
}

// Returns the next character without consuming the current one
static char peekNext() {
    if (isAtEnd()) return '\0';
    return scanner.current[1]; // One character past the current one (its a pointer)
}

// Check if the next character is the expected character (param). If so, consume the current character and return 'true'.
static bool match(char expected) {
    if (isAtEnd()) return false;
    if (*scanner.current != expected) return false;
    scanner.current++;
    return true;
}

// Makes a token
static Token makeToken(TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner.start; // Sets start of token to start of scanner's current lexeme
    token.length = (int)(scanner.current - scanner.start);
    token.line = scanner.line;
    return token;
}

// Makes an error token
static Token errorToken(const char* message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = scanner.line;
    return token;
}

static void skipWhitespace() {
    for (;;) { // To skip more than one character of whitespace
        char c = peek();
        switch(c) {
            // Goes to next character if whitespace
            case ' ':
            case '\r':
            case '\t':
                advance();
                break; // 'break' is used to continue the loop to advance past ALL whitespace
            // Goes to next line and character if newline
            case '\n': 
                scanner.line++;
                advance();
                break;
            // If not whitespace/newline, then return (and exit the loop)
            case '/':
                if (peekNext() == '/') { // If it isn't a double slash, we don't want to mark it as whitespace
                    // A comment goes until the end of the line.
                    while (peek() != '\n' && !isAtEnd()) advance;
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static TokenType checkKeyword(int start, int length, const char* rest, TokenType type) {
    // Checks if the current lexeme is the same length as the keyword, and if their values are equal
    if (scanner.current - scanner.start == start + length && memcmp(scanner.start + start, rest, length) == 0) {
        return type;
    }

    return TOKEN_IDENTIFIER;
}

static TokenType identifierType() {
    switch (scanner.start[0]) {
        case 'a': return checkKeyword(1, 2, "nd", TOKEN_AND);
        case 'c': return checkKeyword(1, 4, "lass", TOKEN_CLASS);
        case 'e': return checkKeyword(1, 3, "lse", TOKEN_ELSE);
        case 'f': 
            if (scanner.current - scanner.start > 1) { // Check if there is 2 or more characters
                switch (scanner.start[1]) {
                    case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
                    case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
                    case 'u': return checkKeyword(2, 1, "n", TOKEN_FUN);
                }
            }
            break; // Fall to return identifier
        case 'i': return checkKeyword(1, 1, "f", TOKEN_IF);
        case 'n': return checkKeyword(1, 2, "il", TOKEN_NIL);
        case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
        case 'p': return checkKeyword(1, 4, "rint", TOKEN_PRINT);
        case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
        case 's': return checkKeyword(1, 4, "uper", TOKEN_SUPER);
        case 't': 
            if (scanner.current - scanner.start > 1) { // Check if there is 2 or more characters
                switch (scanner.start[1]) {
                    case 'h': return checkKeyword(2, 2, "is", TOKEN_THIS);
                    case 'r': return checkKeyword(2, 2, "ue", TOKEN_TRUE);
                }
            }
            break; // Fall to return identifier
        case 'v': return checkKeyword(1, 2, "ar", TOKEN_VAR);
        case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER; 
}

static Token identifier() {
    while (isAlpha(peek()) || isDigit(peek())) advance();
    return makeToken(identifierType());
}

static Token number() {
    // Consume characters while they are digits
    while (isDigit(peek())) advance();

    // Look for a decimal part.
    if (peek() == '.' && isDigit(peekNext())) {
        // Consume the ".".
        advance();

        while (isDigit(peek())) advance();
    }

    return makeToken(TOKEN_NUMBER); // We leave converting literals to runtime values for later
}

static Token string() {
    while(peek() != '"' && !isAtEnd()) { // Goes until the end of the string... or file
        if (peek() == '\n') scanner.line++; // Watching out for newlines. Lox supports multi-line strings.
        advance();
    }

    // If we reach the end of the file without the string ending, it's an unterminated string.
    if (isAtEnd()) return errorToken("Unterminated string");

    // The closing quote.
    advance();
    return makeToken(TOKEN_STRING); // We leave converting literals to runtime values for later
}

// Scans a single token. We don't want to manage a dynamic array for all the tokens, so we just scan them one at a time.
Token scanToken() {
    skipWhitespace();

    // Set the scanner to start at the next new token
    scanner.start = scanner.current;

    // EOF check
    if (isAtEnd()) return makeToken(TOKEN_EOF); 

    char c = advance();
    if (isAlpha(c)) return identifier();
    if (isDigit(c)) return number();

    switch (c) {
        // Single character tokens
        case '(': return makeToken(TOKEN_LEFT_PAREN);
        case ')': return makeToken(TOKEN_RIGHT_PAREN); 
        case '{': return makeToken(TOKEN_LEFT_BRACE);
        case '}': return makeToken(TOKEN_RIGHT_BRACE);
        case ';': return makeToken(TOKEN_SEMICOLON);
        case ',': return makeToken(TOKEN_COMMA);
        case '.': return makeToken(TOKEN_DOT);
        case '-': return makeToken(TOKEN_MINUS);
        case '+': return makeToken(TOKEN_PLUS);
        case '/': return makeToken(TOKEN_SLASH);
        case '*': return makeToken(TOKEN_STAR);
        
        // One or two character tokens
        // If the first character is found, check if the next one is '=', then consume '=' and return corresponding value
        case '!':
            return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=':
            return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<':
            return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>':
            return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);

        // Literals
        case '"': return string();
    }

    // No valid token, so an error is produced.
    return errorToken("Unexpected character.");
}