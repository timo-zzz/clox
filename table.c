#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

void initTable(Table* table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void freeTable(Table* table) {
    FREE_ARRAY(Entry, table->entries, table->capacity); // FREE_ARRAY already checks for NULL arrays
    initTable(table); // Set everything to 0/NULL
}

static Entry* findEntry(Entry* entries, int capacity, ObjString* key) {
    uint32_t index = key->hash % capacity; // Map entry to an index using key hash modulus capacity (basically folds the range on itself so it can fit in the array)

    // Probing loop. Load factor prevents infinite loop.
    for (;;) {
        Entry* entry = &entries[index];
        if (entry->key == key || entry->key == NULL) { // Return the entry at that index if it is either a match or NULL (empty). This also means if this function is being used for an insert, existing keys will have their value replaced.
            return entry;
        }

        index = (index + 1) % capacity; // "Probe" (loop to) the next bucket if there is a collision (different key currently in this bucket). The "% capacity" wraps us to the beginning if we go past the end of the array.
    }
}

// Sets a key-value pair in the table
bool tableSet(Table* table, ObjString* key, Value value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) { // Grow when array is 75% full
        int capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(table, capacity);
    }
    Entry* entry = findEntry(table->entries, table->capacity, key);
    bool isNewKey = entry->key == NULL; // If it is NULL, that means the bucket is empty, meaning it is a new key.
    if (isNewKey) table->count++; 

    entry->key = key;
    entry->value = value;
    return isNewKey;
}