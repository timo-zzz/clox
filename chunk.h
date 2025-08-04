#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
#include "value.h"

typedef enum {
    OP_CONSTANT,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NEGATE,
    OP_RETURN,
} OpCode; // Operation Code

typedef struct {
    int count;
    int capacity;
    uint8_t* code; // Byte array because it is BYTEcode. Took me too long to make that connection.
    int* lines;
    ValueArray constants;
} Chunk; // Chunk of bytecode

void initChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);
void freeChunk(Chunk* chunk);
int addConstant(Chunk* chunk, Value value);

#endif