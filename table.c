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

// Finds an entry or a spot for one
static Entry* findEntry(Entry* entries, int capacity, ObjString* key) {
    uint32_t index = key->hash % capacity; // Map entry to an index using key hash modulus capacity (basically folds the range on itself so it can fit in the array)
    Entry* tombstone = NULL;

    // Probing loop. Load factor prevents infinite loop (since there will always be space, the return statement will always be accessible)
    for (;;) {
        Entry* entry = &entries[index];

        if (entry->key == NULL) {
            if (IS_NIL(entry->value)) {
                // Empty entry
                return tombstone != NULL ? tombstone : entry;
            } else {
                // We found a tombstone, so we return its bucket so it can still be used as space for a new entry
                if (tombstone == NULL) tombstone = entry;
            }
        } else if (entry->key == key) {
            // We found the key, so we return the entry
            return entry;
        }

        index = (index + 1) % capacity; // "Probe" (loop to) the next bucket if there is a collision (different key currently in this bucket). The "% capacity" wraps us to the beginning if we go past the end of the array.
    }
}

// Gets an entry from the table
bool tableGet(Table* table, ObjString* key, Value* value) {
    if (table->count == 0) return false; // Return false if there are no entries

    // Find the entry
    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false; // Return false if the bucket is empty

    // Copy the entry's value to the output parameter
    *value = entry->value;
    return true; // Return true for succesful operation
}

static void adjustCapacity(Table* table, int capacity) {
    // Allocate a new array of buckets/entries
    Entry* entries = ALLOCATE(Entry, capacity);

    // Initialize every entry
    for (int i = 0; i < capacity; i++) {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }

    // Clear count because of tombstones (every entry will be in adjacent buckets anyway since we're re-probing)
    table->count = 0;
    // Check old entry array (if table is being resized)
    for (int i = 0; i < table->capacity; i++) {
        // Entry at this index
        Entry* entry = &table->entries[i];

        // If there was no entry or a tombstone here before, continue
        if (entry->key == NULL) continue;

        // Find spot in new array for this entry using new capacity (this is why it takes an Entry* as an argument as opposed to a table)
        Entry* dest = findEntry(entries, capacity, entry->key); // Stores the spot in memory (its a pointer to the new array)
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++; // Add to entry count
    }

    // Free old array
    FREE_ARRAY(Entry, table->entries, table->capacity);

    // Store new array data in hash table struct
    table->entries = entries;
    table->capacity = capacity;
}

// Adds a key-value pair to the table
bool tableSet(Table* table, ObjString* key, Value value) {
    // Grow when array is 75% full
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) { 
        int capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(table, capacity);
    }
    // Find spot for entry or find existing entry to replace
    Entry* entry = findEntry(table->entries, table->capacity, key);
    bool isNewKey = entry->key == NULL; // If it is NULL, that means the bucket is empty, meaning it is a new key.
    if (isNewKey && IS_NIL(entry->value)) table->count++; // Add to count if it is a new key AND if its not a tombstone (tombstones count as entries)

    // Add the entry
    entry->key = key;
    entry->value = value;
    return isNewKey; // Return if the key is new or not
}

// Deletes an entry from the table
bool tableDelete(Table* table, ObjString* key) {
    if (table->count == 0) return false; // If table is empty, return false.

    // Find the entry so we can delete it
    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;

    // Place a tombstone (special entry so findEntry doesn't stop when probing, since it also finds empty buckets for new entries) to delete the entry
    entry->key == NULL;
    entry->value = BOOL_VAL(true);
}

// Adds all entries from one table to another.
void tableAddAll(Table* from, Table* to) {
    // Loop capacity of "from" table times
    for (int i = 0; i < from->capacity; i++) {
        // Get entry at index i
        Entry* entry = &from->entries[i];

        // Add entry to "to" table
        if (entry->key != NULL) {
            tableSet(to, entry->key, entry->value);
        }
    }
}

// Finds a string in a table
ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash) {
    if (table->count == 0) return NULL; // If table is empty, return null

    uint32_t index = hash % table->capacity; // Map entry to an index using key hash modulus capacity (basically folds the range on itself so it can fit in the array)
    
    // Probing loop. Load factor prevents infinite loop (since there will always be space, the return statement will always be accessible)
    for (;;) {
        Entry* entry = &table->entries[index];
        if (entry->key == NULL) {
            // Stop if we find an empty non-tombstone entry.
            if (IS_NIL(entry->value)) return NULL;
        } else if (entry->key->length == length && entry->key->hash == hash && memcmp(entry->key->chars, chars, length) == 0) {
            // We found the string!
            return entry->key;
        }

        index = (index + 1) % table->capacity; // Wrap around if we didnt find it.
    }
}