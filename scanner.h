#ifndef clox_scanner_h
#define clox_scanner_h

typedef enum {
    // Single-character tokens
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
    TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
    TOKEN_SEMICOLON, TOKEN_SLASH, TOKEN_STAR,
    // One or two character tokens
    TOKEN_BANG, TOKEN_BANG_EQUAL,
    TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    TOKEN_LESS, TOKEN_LESS_EQUAL,
    // Literals!!!! (idk i rlly like literals for some reason)
    TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER,
    // Keywords
    TOKEN_AND, TOKEN_CLASS, TOKEN_ELSE, TOKEN_FALSE,
    TOKEN_FOR, TOKEN_FUN, TOKEN_IF, TOKEN_NIL, TOKEN_OR,
    TOKEN_PRINT, TOKEN_RETURN, TOKEN_SUPER, TOKEN_THIS,
    TOKEN_TRUE, TOKEN_VAR, TOKEN_WHILE,

    // The spoiled kids who think they get to end everybody's FUN (they do though)
    TOKEN_ERROR, TOKEN_EOF
    // An error token is needed because the only errors that happen during scanning aren't automatically handled by C

    /* We have a lot of comments here. Might as well sit down and talk for a bit.
       I'm listening to Glass Beach. Their song called "glass beach".
       My dad died, what, bout 24 hours and 39 minutes ago?
       This artist reminds me a lot of the memories we had together.
       I miss you dad. This one is for you man. I should've spent more time with you... I'm tearing up writing this.
       You're gone forever.
       If you're reading this, go do something better with your time. Like spending time with someone you love.
       I hate multi-line comments. Ok rant over bye
       I don't think I've heard this one before. This song definitely took inspiration from Bohemian Rhaspody. 
    */
} TokenType;

typedef struct {
    TokenType type;
    // We just pass the pointer to the start so we don't gotta memory manage that crap. All of these tokens are just part of the original source code string.
    const char* start;
    int length;
    int line;
} Token;

void initScanner(const char* source);
Token scanToken();

#endif