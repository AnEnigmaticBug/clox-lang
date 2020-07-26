#ifndef CLOX_TABLE_H
#define CLOX_TABLE_H

#include "common.h"
#include "value.h"

typedef struct
{
    ObjString *key;
    Value val;
} Entry;

typedef struct
{
    int count;
    int capacity;
    Entry *entries;
} Table;

void init_table(Table *table);
void free_table(Table *table);
bool table_get(Table *table, ObjString *key, Value *val);
bool table_put(Table *table, ObjString *key, Value val);
void table_put_all(Table *from, Table *to);
bool table_remove(Table *table, ObjString *key);
ObjString *table_find_string(Table *table, const char *chars, int length, uint32_t hash);
void table_remove_white(Table *table);
void mark_table(Table *table);

#endif
