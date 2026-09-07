#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
#include "vm.h"

VM vm;

// Creates a Lox function for the C clock() function
static Value clockNative(int argCount, Value* args) {
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

// Resets the stack. Wow! I would've never guessed!
static void resetStack() {
    vm.stackTop = vm.stack; // Resets the stack to the beginning (moves where we are on the stack right now back to the start)
    vm.frameCount = 0; // Reset the amount of frames 
}

static void runtimeError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    // Print out a stack trace. Prints out each function that was executing when the program errored.
    for (int i = vm.frameCount - 1; i >= 0; i--) {
        // Get the current function's info (its a loop)
        CallFrame* frame = &vm.frames[i];
        ObjFunction* function = frame->function;
        size_t instruction = frame->ip - function->chunk.code - 1;

        // Print out the function's info
        fprintf(stderr, "[line %d] in ",
                function->chunk.lines[instruction]);
        if (function->name == NULL) { // Print out script for the top-level function
            fprintf(stderr, "script\n");
        } else { // Print out the function's name if its a real function
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    CallFrame* frame = &vm.frames[vm.frameCount - 1]; // Get the topmost function call frame
    size_t instruction = frame->ip - frame->function->chunk.code - 1; // ip minus the start of the chunk (so where the ip started) gets us the # of how far the ip has advanced, therefore what index we are currently at in the chunk's bytecode array.
    int line = frame->function->chunk.lines[instruction];
    fprintf(stderr, "[line %d] in script\n", line);
    resetStack();
}

// Used to define a native function in Lox
static void defineNative(const char* name, NativeFn function) {
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    push(OBJ_VAL(newNative(function)));
    tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

void initVM() {
    resetStack();
    vm.objects = NULL;

    initTable(&vm.globals); // Global variable table
    initTable(&vm.strings); // Interned string table

    defineNative("clock", clockNative);
}

void freeVM() {
    freeTable(&vm.globals);
    freeTable(&vm.strings); 
    freeObjects();
}

void push(Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop() {
    vm.stackTop--;
    return *vm.stackTop;
}

// Returns a Value from the stack without popping it. Accesses the index at the top of stack - 1 - distance.
static Value peek(int distance) {
    // stackTop is a pointer to the top of the stack, so this is doing pointer math to access -1 - distance from the top of the stack.
    return vm.stackTop[-1 - distance];
}

// The method for a call
static bool call(ObjFunction* function, int argCount) {
    // Check if the argCount matches the number of parameters the function should have
    if (argCount != function->arity) {
        runtimeError("Expected %d arguments but got %d.",
            function->arity, argCount);
        return false; // Return that the call failed
    }

    // If we are too many frames deep, error. The CallFrame array's size is fixed.
    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
    }

    // Increase our frame/scope depth
    CallFrame* frame = &vm.frames[vm.frameCount++];

    // Initialize the CallFrame
    frame->function = function;
    frame->ip = function->chunk.code; // Set ip to the start of the function's bytecode array (which is also a (decayed) pointer! The ip just points to our current location during our traversal of that array).
    frame->slots = vm.stackTop - argCount - 1; // Give the frame its correct window on the stack
    return true; // Return that the call was successful
}

// Checks if a call is valid. If it is, then call.
static bool callValue(Value callee, int argCount) {
    // Check if the Value is actually a callable Obj
    if (IS_OBJ(callee)) {
        switch(OBJ_TYPE(callee)) {
            case OBJ_FUNCTION:
                return call(AS_FUNCTION(callee), argCount);
            case OBJ_NATIVE: {
                // Get the C function pointer, then use that pointer to call it. No need for CallFrames, C handles all that nasty stuff.
                NativeFn native = AS_NATIVE(callee);
                Value result = native(argCount, vm.stackTop - argCount);

                // Even though we didn't use a CallFrame, we still need to remove the function call's arguments from the stack  
                vm.stackTop -= argCount + 1;
                push(result);
                return true;
            }
            default:
                break; // Fall through if it's a non-callable object type
        }
    }
    // Return an error if this object type isn't callable
    runtimeError("Can only call functions and classes.");
    return false; // Return that the call was unsuccessful
}

static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
    ObjString* b = AS_STRING(pop());
    ObjString* a = AS_STRING(pop());

    // Calculate length of new string
    int length = a->length + b->length;

    // Allocate new string
    char* chars = ALLOCATE(char, length + 1);

    // Copy chars to new stirng (in the right order)
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);

    // Add null terminator
    chars[length] = '\0';

    // Wrap the string into an ObjString, then push it onto the stack
    ObjString* result = takeString(chars, length);
    push(OBJ_VAL(result));
}

static InterpretResult run() {
    // Get the address of first callframe (needs to be a pointer, otherwise C just stores a copy), which stores our IP and local variables. 
    CallFrame* frame = &vm.frames[vm.frameCount - 1];

// Reads one byte of bytecode from the stack. Remember, the IP is a pointer.
#define READ_BYTE() (*frame->ip++) // The IP (instruction pointer) always points to the next byte of code. This then dereferences it (reads a byte).

#define READ_SHORT() \
    (frame->ip += 2, \
    (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1])) // Reads a short (16-bit) number from the stack

#define READ_CONSTANT() (frame->function->chunk.constants.values[READ_BYTE()]) // Gets a constant from the constant table. The bytecode array stores the index of a Value in the constant pool.

#define READ_STRING() AS_STRING(READ_CONSTANT()) // Reads a one byte operand (the idx of the string) and returns the string at that index.
#define BINARY_OP(valueType, op) \
    do { \
        /* Binary operations are pushed onto the stack in this order: operator, left operand, right operand */ \
        if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
            runtimeError("Operands must be numbers."); \
        } \
        double b = AS_NUMBER(pop()); \
        double a = AS_NUMBER(pop()); \
        push(valueType(a op b)); \
    } while (false)

    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        printf("          ");
        for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
            printf("[ ");
            printValue(*slot);
            printf(" ]");
        }
        printf("\n");
        disassembleInstruction(&frame->function->chunk, 
            (int)(frame->ip - frame->function->chunk.code));
#endif
        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            case OP_NIL: push(NIL_VAL); break;
            case OP_TRUE: push(BOOL_VAL(true)); break;
            case OP_FALSE: push(BOOL_VAL(false)); break;
            case OP_POP: pop(); break;
            case OP_GET_LOCAL: {
                // Push a local variable's value onto the stack
                uint8_t slot = READ_BYTE(); // Should be the stack slot where the local is
                push(frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE(); // Should be the stack slot where the local is
                frame->slots[slot] = peek(0);
                break;
            }
            case OP_GET_GLOBAL: {
                // Get the string at that index (next instruction)
                ObjString* name = READ_STRING();
                Value value;

                // Value is output param
                if (!tableGet(&vm.globals, name, &value)) {
                    // If the key does not exist, the string isn't defined, so throw an error.
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }

                // Push the value onto the stack (once again, output param)
                push(value);
                break;
            }
            case OP_DEFINE_GLOBAL: {
                // Get the string at that index (next instruction)          
                ObjString* name = READ_STRING();
                tableSet(&vm.globals, name, peek(0));
                // We wait to pop incase garbage collection is triggered while adding the value to the table
                pop();
                break;
            }
            case OP_SET_GLOBAL: {
                // Get the string at that index (next instruction)
                ObjString* name = READ_STRING();

                if (tableSet(&vm.globals, name, peek(0))) { // Re-assigns/sets an existing variable's value (function has a side effect)
                    // If key added is new (undefined variable) thrown an error
                    tableDelete(&vm.globals, name);
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_GREATER:  BINARY_OP(BOOL_VAL, >); break;
            case OP_LESS:     BINARY_OP(BOOL_VAL, <); break;
            case OP_ADD: {
                // String concatenation
                if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                    concatenate();
                // Number addition
                } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(NUMBER_VAL(a + b));
                } else {
                    runtimeError("Operands must be two numbers or two strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;
            case OP_NOT:
                // Pop the bool, operate on it, then push it
                push(BOOL_VAL(isFalsey(pop())));
                break;
            case OP_NEGATE: 
                // Check if operand is a number  
                if (!IS_NUMBER(peek(0))) {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                // Unwrap the Value, negate it, and then wrap it back up
                push(NUMBER_VAL(-AS_NUMBER(pop())));
            case OP_PRINT: {
                printValue(pop());
                printf("\n");
                break;
            }
            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                // Read offset operand
                uint16_t offset = READ_SHORT();
                // Jump if value is false
                if (isFalsey(peek(0))) frame->ip += offset;
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset;
                break;
            }
            case OP_CALL: {
                int argCount = READ_BYTE(); // argCount was emitted onto the stack as an operand
                // If the call fails, return a runtime error. callValue's side effect executes the call if the call is succesful.
                if (!callValue(peek(argCount), argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm.frames[vm.frameCount - 1]; // Move back a frame after calling the function
                break;
            }
            case OP_RETURN: {
                // Save the function's result, since we're about to pop the function's stack/CallFrame
                Value result = pop(); 

                // Go back a frame, since we're done with a function
                vm.frameCount--; 

                // If the frame count is 0, that means we're done with the program. So we pop the top-level function and exit the interpreter.
                if (vm.frameCount == 0) {
                    pop();
                    return INTERPRET_OK;
                }

                vm.stackTop = frame->slots; // The top of the stack is now the beginning of that function's frame
                push(result); // Push the function's result back onto the stack
                frame = &vm.frames[vm.frameCount - 1]; // Go back to the former frame
                break;
            }
        }
    }

#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

// Takes source code and inteprets/runs it
InterpretResult interpret(const char* source) {
    // Compile our source code into bytecode.
    ObjFunction* function = compile(source); 
    if (function == NULL) return INTERPRET_COMPILE_ERROR; // Compiler will always return NULL if theres errors

    // Store the top-level, implicit main function on the stack
    push(OBJ_VAL(function));
    // Prepare the function's call frame so it can be executed
    call(function, 0);

    return run();
}



