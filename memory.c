#include <stdlib.h>

#include "memory.h" // Includes object.h
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

// Used to reallocate or free memory. All memory management goes through this function so the VM can track memory (we free memory using the FREE() macro, which uses this method)
void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    // Run the GC when new memory is allocated.
    // The if statement ensures we don't run the GC when we free memory (which would make the GC... recursive.)
    if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
        collectGarbage();
#endif
    }
    
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
#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void*)object, object->type);
#endif

    switch(object->type) {
        case OBJ_CLOSURE: {
            // Free the closure's upvalue array
            ObjClosure* closure = (ObjClosure*)object;
            FREE_ARRAY(ObjUpvalue*, closure->upvalues,
                        closure->upvalueCount);
            // We don't need to free the ObjFunction member since multiple closures can own the same ObjFunction. The ObjFunction can only be freed after all the closures using it are freed, which the GC will handle.
            FREE(ObjClosure, object);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object; // Cast the Obj argument to its real type
            freeChunk(&function->chunk); // Free the chunk holding all the function's code
            // We don't need to free the function's name since its an ObjString, which the garbage collector will handle that.
            FREE(ObjFunction, object);
            break;
        }
        case OBJ_NATIVE: {
            FREE(ObjNative, object); // ObjNative's members don't allocate any memory, thankfully
            break;
        }
        case OBJ_STRING: {
            ObjString* string = (ObjString*)object; // Cast the Obj argument to its real type
            FREE_ARRAY(char, string->chars, string->length + 1); // The raw string was allocated on the heap too, so we must also free that.
            FREE(ObjString, object);
            break;
        }
        case OBJ_UPVALUE: {
            FREE(ObjUpvalue, object);
            break;
        }
    }
}

void collectGarbage() {
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
#endif

#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
#endif
}

void freeObjects() {
    Obj* object = vm.objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }
}