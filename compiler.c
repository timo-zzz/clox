#include <stdio.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"

void compile(const char* source) {
    initScanner(source);
    int line = -1;
    for (;;) {
        Token token = scanToken(); // We don't want to manage a dynamic array for all the tokens, so we just scan them one at a time.
        if (token.line != line) {
            printf("%4d ", token.line); // Print out the line of the current token
            line = token.line; 
        } else {
            printf("   | "); // Indicates token is on the same line as the previous line
        }
        printf("%2d '%.*s'\n", token.type, token.length, token.start); // Print out information about the token

        if (token.type == TOKEN_EOF) break; // Break loop at EOF token (end of file)
    }
}