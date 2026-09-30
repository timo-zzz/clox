#include <stdlib.h>

#include "compiler.h"
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

// Marks an Obj's isMarked field 
void markObject(Obj* object) {
    if (object == NULL) return;
    if (object->isMarked) return; // Make sure we only mark white objects
#ifdef DEBUG_LOG_GC
    // Log when we mark a variable
    printf("%p mark ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif

    object->isMarked = true;

    // If the amount of gray objects is about to exceed capacity, increase the stack's capacity
    if (vm.grayCapacity < vm.grayCount + 1) {
        vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
        // We use C's realloc instead of ours because we don't want the grayStack to be managed by the GC
        // and start a recursive GC
        vm.grayStack = (Obj**)realloc(vm.grayStack,
                                 sizeof(Obj*) * vm.grayCapacity);
        
        // If we fail to allocate the stack, we can't finish garbage collection. So end program.
        if (vm.grayStack == NULL) exit(1); 
    }

    // Add this marked object to our list of gray objects
    vm.grayStack[vm.grayCount++] = object;
}

// Marks a value as gray and reachable (meaning possibly usable), signifying to our GC that it should NOT be freed.
void markValue(Value value) {
    // Just checks if the Value is an Obj (since thats the only value thats dynamically allocated)
    if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

// Marks an array of values as reachable and gray
static void markArray(ValueArray* array) {
    for (int i = 0; i < array->count; i++) {
        markValue(array->values[i]);
    }
}

// Blackens an object, meaning that it is reachable and we have marked everything it references as gray.
// A black object is an object that has its isMarked field set and is no longer in the gray stack. There is no "black" field.
static void blackenObject(Obj* object) {
#ifdef DEBUG_LOG_GC
    // Log when we blacken an Obj and some details about the Obj
    printf("%p blacken ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif

    // Trace inner references & add them to the gray stack.
    switch (object->type) {
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            markObject((Obj*)closure->function); // Mark ObjFunction
            // Mark all the upvalues the closure captures
            for (int i = 0; i < closure->upvalueCount; i++) {
                markObject((Obj*)closure->upvalues[i]);
            }
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            markObject((Obj*)function->name); // ObjString
            markArray(&function->chunk.constants); // ValueArray
            break;
        }
        case OBJ_UPVALUE:
            // Trace closed upvalues (that are on the heap)
            markValue(((ObjUpvalue*)object)->closed);
            break;
        // These don't have any references to other Objs
        case OBJ_NATIVE:
        case OBJ_STRING:
            break;
    }
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

// Mark roots, or any value the VM can directly access.
static void markRoots() {
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
        markValue(*slot);
    }

    // Mark closures in the VM's CallFrames
    for (int i = 0; i < vm.frameCount; i++) {
        markObject((Obj*)vm.frames[i].closure);
    }

    // Mark open upvalues
    for (ObjUpvalue* upvalue = vm.openUpvalues;
         upvalue != NULL;
         upvalue = upvalue->next) {
        markObject((Obj*)upvalue);
    }
    
    markTable(&vm.globals);
    markCompilerRoots();
}

static void traceReferences() {
    // Iterate over the grayStack and blacken each gray object
    while (vm.grayCount > 0) {
        Obj* object = vm.grayStack[--vm.grayCount];
        blackenObject(object);
    }
}

static void sweep() {
    Obj* previous = NULL;
    Obj* object = vm.objects;
    // Iterate over the whole Obj linked list
    while (object != NULL) {
        // If Obj isMarked, just move onto the next Obj
        if (object->isMarked) {
            object->isMarked = false; // Turn each black Obj back to white, resetting the GC
            previous = object;
            object = object->next;
        } else { // If Obj is white, free it!
            Obj* unreached = object;
            object = object->next;
            // Patch up the Obj linked list since we're freeing this Obj
            if (previous != NULL) {
                previous->next = object;
            } else { // If the first Obj is white, make the next Obj the first in the linked list
                vm.objects = object;
            }

            // Free the white Obj
            freeObject(unreached);
        }
    }
}

// I bet you can't guess what this function does!
void collectGarbage() {
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
#endif

    markRoots();
    traceReferences(); // Doesn't treat string table as roots. If we did, we would never free any string!
    tableRemoveWhite(&vm.strings);
    sweep();

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

    free(vm.grayStack);
}