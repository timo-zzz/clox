#ifndef clox_compiler_h
#define clox_compiler_h

#include "object.h"
#include "vm.h"

ObjFunction* compile(const char* source); // Returns the function the VM will run.
#endif