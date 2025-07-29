#include <stdio.h>

#include "debug.h"
#include "value.h"

void disassembleChunk(Chunk* chunk, const char* name) {
    printf("== %s ==\n", name);

    for (int offset = 0; offset < chunk->count;) {
        offset = disassembleInstruction(chunk, offset); // Increments offset for us
    }
}

static int constantInstruction(const char* name, Chunk* chunk, int offset) {
    uint8_t constant = chunk->code[offset + 1]; // Index of constant
    printf("%-16s %4d '", name, constant); // Print index of constant and the constant
    printValue(chunk->constants.values[constant]); 
    printf("'\n");
    return offset + 2; // OP_CONSTANT is 2 bytes (one for the opcode and one for the operand), hence why we increment by 2.
}

static int simpleInstruction(const char* name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}

int disassembleInstruction(Chunk* chunk, int offset) {
    printf("%04d ", offset); // Print offset position of instruction
    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
        printf("   | "); // If same line as previous instruction, print this.
    } else {
       printf("%4d ", chunk->lines[offset]); // Else, print the line number.
    }

    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_CONSTANT:
            return constantInstruction("OP_CONSTANT", chunk, offset);
        case OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);
        default:
            printf("Unknown opcode %d\n");
            return offset + 1;
    }
}