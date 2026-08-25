#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "chunk.h"
#include "value.h"

#define OBJ_TYPE(value)     (AS_OBJ(value)->type)

#define IS_FUNCTION(value)  isObjType(value, OBJ_FUNCTION); /* Used to check if Objs are functions, for safe casting. */
#define IS_STRING(value)    isObjType(value, OBJ_STRING) /* Used to check if Objs are strings, for safe casting. */

#define AS_FUNCTION(value)  ((ObjFunction*)AS_OBJ(value)) /* Used to cast Objs to ObjFunctions, assuming it is safe. */
#define AS_STRING(value)    ((ObjString*)AS_OBJ(value)) /* Used to cast Objs to ObjStrings, assuming it is safe. */
#define AS_CSTRING(value)   (((ObjString*)AS_OBJ(value))->chars) /* Used to cast Objs/ObjStrings to a C string, assuming it is safe. */

typedef enum {
    OBJ_FUNCTION,
    OBJ_STRING,
} ObjType;

struct Obj {
    ObjType type;
    struct Obj* next;
}; // No typedef because it was forward declared in value.h

typedef struct {
    // Having Obj as the first value allows ObjFunction to be safely casted to an Obj, and vice-versa. This also means that they share behavior and state, almost like inheritance in OOP.
    Obj obj; 
    int arity; // Number of parameters the function has
    Chunk chunk; // The bytecode chunk holding the function's code
    ObjString* name; // The function's name, represented as a Lox object.
} ObjFunction; // Represents a Lox function object. Lox functions need to be object because functions are first class in Lox.

struct ObjString {
    // Having Obj as the first value allows ObjString to be safely casted to an Obj, and vice-versa. This also means that they share behavior and state, almost like inheritance in OOP.
    Obj obj; 
    int length;
    char* chars; // Stored on heap
    uint32_t hash; // We cache (store it in the string) a string's hash so we don't have to re-calculate the hash everytime we look for a key.
}; // No typedef because it was forward declared in value.h

ObjFunction* newFunction(); // Initializes a new function
ObjString* takeString(char* chars, int length);
ObjString* copyString(const char* chars, int length);
void printObject(Value value);

// Not put into macro body because "value" is referred to twice.
static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif