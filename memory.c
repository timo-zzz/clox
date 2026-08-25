#include <stdlib.h>

#include "memory.h"
#include "vm.h"

// Used to reallocate or free memory. All memory management goes through this function so the VM can track memory (we free memory using the FREE() macro, which uses this method)
void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    // realloc() returns where the pointer is realloacted to.
    void* result = realloc(pointer, newSize);
    if (result == NULL) exit(1); // realloc() returns NULL if the system is out of memory.
    return result;
}

static void freeObject(Obj* object) {
    switch(object->type) {
        case OBJ_STRING: {
            ObjString* string = (ObjString*)object; // Cast the Obj argument to its real type
            FREE_ARRAY(char, string->chars, string->length + 1); // The raw string was allocated on the heap too, so we must also free that.
            FREE(ObjString, object);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object; // Cast the Obj argument to its real type
            freeChunk(&function->chunk); // Free the chunk holding all the function's code
            // We don't need to free the function's name since its an ObjString, which the garbage collector will handle.
            FREE(ObjFunction, object);
            break;
        }
    }
}

void freeObjects() {
    Obj* object = vm.objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }
}