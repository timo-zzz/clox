#ifndef clox_vm_h
#define clox_vm_h

#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64 // There is a maximum call depth in clox.
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct {
    ObjFunction* function; // Pointer to the function thats being called
    uint8_t* ip; // Instruction pointer, points to the current bytecode instruction. Represents the return address in a function call.
    Value* slots; // First slot in the VM's value stack that the function can use
} CallFrame; // Represents one single ongoing function call. One of these structs are created everytime we call a Lox function.

typedef struct {
    CallFrame frames[FRAMES_MAX]; // A stack of CallFrames. Our chunks of bytecode are part of functions. Once again, all code is sort of wrapped in an implicit main function.
    int frameCount; // Represents how many CallFrames high the stack currently is.
    Value stack[STACK_MAX];
    Value* stackTop; // Always points to the element after the element last pushed onto the stack
    Table globals; // Global variables
    Table strings; // Interned strings
    Obj* objects;
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);
void push(Value value);
Value pop();

#endif