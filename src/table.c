#include "table.h"
#include <string.h>
#include "memory.h"

#define TABLE_MAX_LOAD 0.75

void init_table(Table *table)
{
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void free_table(Table *table)
{
    FREE_ARRAY(table->entries, Entry, table->capacity);
    init_table(table);
}

static Entry *find_entry(Entry *entries, int capacity, ObjString *key)
{
    uint32_t index = key->hash % capacity;
    Entry *tombstone = NULL;

    while (true)
    {
        Entry *entry = &entries[index];

        if (entry->key == NULL)
        {
            if (IS_NIL(entry->val))
            {
                return tombstone != NULL ? tombstone : entry;
            }
            else
            {
                if (tombstone == NULL)
                {
                    tombstone = entry;
                }
            }
        }
        else if (entry->key == key)
        {
            return entry;
        }

        index = (index + 1) % capacity;
    }
}

static void adjust_capacity(Table *table, int capacity)
{
    Entry *entries = ALLOCATE(Entry, capacity);
    for (int i = 0; i < capacity; ++i)
    {
        entries[i].key = NULL;
        entries[i].val = NIL_VAL;
    }

    table->count = 0;

    for (int i = 0; i < table->capacity; ++i)
    {
        Entry *entry = &table->entries[i];
        if (entry->key == NULL)
        {
            continue;
        }

        Entry *dest = find_entry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->val = entry->val;

        ++table->count;
    }

    FREE_ARRAY(table->entries, Entry, table->capacity);

    table->entries = entries;
    table->capacity = capacity;
}

bool table_get(Table *table, ObjString *key, Value *val)
{
    if (table->count == 0)
    {
        return false;
    }

    Entry *entry = find_entry(table->entries, table->capacity, key);
    if (entry->key == NULL)
    {
        return false;
    }

    *val = entry->val;
    return true;
}

bool table_put(Table *table, ObjString *key, Value val)
{
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD)
    {
        int capacity = GROW_CAPACITY(table->capacity);
        adjust_capacity(table, capacity);
    }

    Entry *entry = find_entry(table->entries, table->capacity, key);

    bool is_new_key = entry->key == NULL;
    if (is_new_key && IS_NIL(entry->val))
    {
        ++table->count;
    }

    entry->key = key;
    entry->val = val;

    return is_new_key;
}

void table_put_all(Table *from, Table *to)
{
    for (int i = 0; i < from->capacity; ++i)
    {
        Entry *entry = &from->entries[i];
        if (entry->key != NULL)
        {
            table_put(to, entry->key, entry->val);
        }
    }
}

bool table_remove(Table *table, ObjString *key)
{
    if (table->count == 0)
    {
        return false;
    }

    Entry *entry = find_entry(table->entries, table->capacity, key);

    if (entry->key == NULL)
    {
        return false;
    }

    entry->key = NULL;
    entry->val = BOOLEAN_VAL(true);

    return true;
}

ObjString *table_find_string(Table *table, const char *chars, int length, uint32_t hash)
{
    if (table->count == 0)
        return NULL;

    uint32_t index = hash % table->capacity;

    while (true)
    {
        Entry *entry = &table->entries[index];

        if (entry->key == NULL)
        {
            // Stop if we find an empty non-tombstone entry.
            if (IS_NIL(entry->val))
            {
                return NULL;
            }
        }
        else if (entry->key->length == length &&
                 entry->key->hash == hash &&
                 memcmp(entry->key->chars, chars, length) == 0)
        {
            // We found it.
            return entry->key;
        }

        index = (index + 1) % table->capacity;
    }
}

void table_remove_white(Table *table)
{
    for (int i = 0; i < table->capacity; ++i)
    {
        Entry *entry = &table->entries[i];
        if (entry->key != NULL && !entry->key->obj.is_marked)
        {
            table_remove(table, entry->key);
        }
    }
}

void mark_table(Table *table)
{
    for (int i = 0; i < table->capacity; ++i)
    {
        Entry *entry = &table->entries[i];
        mark_obj((Obj *)entry->key);
        mark_value(entry->val);
    }
}
