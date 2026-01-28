#ifndef clox_table_h
#define clox_table_h

#include "common.h"
#include "value.h"

typedef struct {
    ObjString* key; // Keys are always strings, so this can be an ObjString instead of a value
    Value value;
} Entry;

typedef struct {
    int count; // Number of pairs currently stored
    int capacity;
    Entry* entries;
} Table;

void initTable(Table* table);
void freeTable(Table* table);
bool tableSet(Table* table, ObjString* key, Value value);

#endif