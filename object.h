#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "value.h"

#define OBJ_TYPE(value)     (AS_OBJ(value)->type)

#define IS_STRING(value)    isObjType(value, OBJ_STRING) /* Used to check if Objs are strings, for safe casting. */

#define AS_STRING(value)    ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)   (((ObjString*)AS_OBJ(value))->chars)

typedef enum {
    OBJ_STRING,
} ObjType;

struct Obj {
    ObjType type;
    struct Obj* next;
}; // No typedef because it was forward declared in value.h

struct ObjString {
    // Having Obj as the first value allows ObjStrings to be safely casted to an Obj, and vice-versa. This also means that they share behavior and state, almost like inheritance in OOP.
    Obj obj; 
    int length;
    char* chars; // Stored on heap
}; // No typedef because it was forward declared in value.h

ObjString* takeString(char* chars, int length);
ObjString* copyString(const char* chars, int length);
void printObject(Value value);

// Not put into macro body because "value" is referred to twice.
static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif