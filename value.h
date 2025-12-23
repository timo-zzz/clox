#ifndef clox_value_h
#define clox_value_h

#include "common.h"

typedef enum {
    VAL_BOOL,
    VAL_NIL,
    VAL_NUMBER,
} ValueType; // Represents a Lox value type

typedef struct {
    ValueType type;
    union {
        bool boolean;
        double number;
    } as;
} Value; // Represents a Lox value

// These macros check the ValueType of a Lox Value
#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_NUMBER)

// These macros unwrap a C value from a Lox Value of a specific type
#define AS_BOOL(value)    ((value).as.number)
#define AS_NUMBER(value)  ((value).as.number)

// These macros initialize a Lox Value of a specific type using compound literals
#define BOOL_VAL(value)   ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL           ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = value}})

typedef struct {
    int capacity;
    int count;
    Value* values;
} ValueArray; // Used for const pool

bool valuesEqual(Value a, Value b);
void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValue(Value value);

#endif