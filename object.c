#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

// Allocates an object on the heap, then initializes type. The size is passed so the caller can add bytes for extra fields needed by specific objects.
static Obj* allocateObject(size_t size, ObjType type) {
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;

    object->next = vm.objects;
    vm.objects = object;
    return object;
}

// Creates an ObjString on the heap, then intializes its fields (like a constructor!).
static ObjString* allocateString(char* chars, int length, uint32_t hash) {
    ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING); // If this is a ObjString constructor, ALLOCATE_OBJ is like the Obj superclass constructor.
    string->length = length;
    string->hash = hash;
    string->chars = chars;
    return string;
}

static uint32_t hashString(char* chars, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
}

// Creates a ObjString from a string already allocated onto the heap.
ObjString* takeString(char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    return allocateString(chars, length, hash);
}

// Copies a string from our compiler's stack to the heap, then makes an ObjString from it.
ObjString* copyString(const char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    char* heapChars = ALLOCATE(char, length + 1); // Allocate a char array of length + 1 on the heap
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0'; // String terminator character, since the parser string is one long, unterminated one
    return allocateString(heapChars, length, hash);
}

void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
    }
}