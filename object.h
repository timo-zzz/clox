#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "chunk.h"
#include "value.h"

#define OBJ_TYPE(value)     (AS_OBJ(value)->type)

// Keep the comments on the sides so each macro has its own comment that shows up when you hover over it :)
#define IS_CLOSURE(value)   isObjType(value, OBJ_CLOSURE) /* Used to check if Objs are closures, for safe casting. */
#define IS_FUNCTION(value)  isObjType(value, OBJ_FUNCTION) /* Used to check if Objs are functions, for safe casting. */
#define IS_NATIVE(value)    isObjType(value, OBJ_NATIVE) /* Used to check if Objs are native functions, for safe casting. */
#define IS_STRING(value)    isObjType(value, OBJ_STRING) /* Used to check if Objs are strings, for safe casting. */

#define AS_CLOSURE(value)   ((ObjClosure*)AS_OBJ(value)) /* Used to cast Objs to ObjClosures, assuming it is safe. */
#define AS_FUNCTION(value)  ((ObjFunction*)AS_OBJ(value)) /* Used to cast Objs to ObjFunctions, assuming it is safe. */
#define AS_NATIVE(value) \
    (((ObjNative*)AS_OBJ(value))->function) /* Used to get the corresponding C function pointer from a native function */
#define AS_STRING(value)    ((ObjString*)AS_OBJ(value)) /* Used to cast Objs to ObjStrings, assuming it is safe. */
#define AS_CSTRING(value)   (((ObjString*)AS_OBJ(value))->chars) /* Used to cast Objs/ObjStrings to a C string, assuming it is safe. */

typedef enum {
    OBJ_CLOSURE,
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_STRING,
    OBJ_UPVALUE
} ObjType;

struct Obj {
    ObjType type;
    struct Obj* next;
}; // No typedef because it was forward declared in value.h, which is included in this file.

typedef struct {
    // Having Obj as the first value allows ObjFunction to be safely casted to an Obj, and vice-versa. This also means that they share behavior and state, almost like inheritance in OOP.
    Obj obj; 
    int arity; // Number of parameters the function has
    int upvalueCount; // Count of variables in surrounding (non-global) scopes
    Chunk chunk; // The bytecode chunk holding the function's code
    ObjString* name; // The function's name, represented as a Lox object.
} ObjFunction; // Represents a Lox function object. Lox functions need to be object because functions are first class in Lox.

// Function pointer wrapper for a native C function
typedef Value (*NativeFn)(int argCount, Value* args);

typedef struct {
    // Having Obj as the first value allows ObjNative to be safely casted to an Obj, and vice-versa. This also means that they share behavior and state, almost like inheritance in OOP.
    Obj obj;
    NativeFn function;
} ObjNative; // Represents a native C function converted to a Lox function.

struct ObjString {
    // Having Obj as the first value allows ObjString to be safely casted to an Obj, and vice-versa. This also means that they share behavior and state, almost like inheritance in OOP.
    Obj obj; 
    int length;
    char* chars; // Stored on heap
    uint32_t hash; // We cache (store it in the string) a string's hash so we don't have to re-calculate the hash everytime we look for a key.
}; // No typedef because it was forward declared in value.h, which is included in this file.

typedef struct ObjUpvalue {
    // Having Obj as the first value allows ObjUpvalue to be safely casted to an Obj, and vice-versa. This also means that they share behavior and state, almost like inheritance in OOP.
    Obj obj; 
    Value* location; // Pointer to the variable. Could be on the stack or heap. This means that the inner function should be able to read AND write to the variable.
    Value closed; // Place on the heap for the upvalue to live if it becomes closed
    struct ObjUpvalue* next; // Linked list!
} ObjUpvalue; // Represents a single captured upvalue

typedef struct {
    // Having Obj as the first value allows ObjClosure to be safely casted to an Obj, and vice-versa. This also means that they share behavior and state, almost like inheritance in OOP.
    Obj obj;
    ObjUpvalue** upvalues; // The closure's upvalue array
    int upvalueCount; // Though ObjFunction already has this field, the GC needs this.
    ObjFunction* function;
} ObjClosure; // Holds captured runtime values. This is needed because closures need runtime values, but ObjFunctions only hold a compile-time representation.

ObjClosure* newClosure(ObjFunction* function);
ObjFunction* newFunction(); // Initializes a new function
ObjNative* newNative(NativeFn function); // Initalizes a new native function
ObjString* takeString(char* chars, int length);
ObjString* copyString(const char* chars, int length);
ObjUpvalue* newUpvalue(Value* slot); // Argument is a pointer to where the variable lives; it's a pointer to which local array slot the variable is in.
void printObject(Value value);

// Not put into macro body because "value" is referred to twice.
static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif