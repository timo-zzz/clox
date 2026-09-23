#ifndef clox_common_h
#define clox_common_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// #define DEBUG_PRINT_CODE
// #define DEBUG_TRACE_EXECUTION

#define DEBUG_STRESS_GC // Runs the GC at every possible moment to test it
#define DEBUG_LOG_GC // Logs things when the GC is running so we can see what its doing

#define UINT8_COUNT (UINT8_MAX + 1)

#endif