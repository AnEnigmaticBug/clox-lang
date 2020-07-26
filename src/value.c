#include "value.h"
#include <stdio.h>
#include <string.h>
#include "memory.h"
#include "obj.h"

bool values_equal(Value a, Value b)
{
    if (a.type != b.type)
    {
        return false;
    }

    switch (a.type)
    {
    case VAL_BOOLEAN:
        return AS_BOOLEAN(a) == AS_BOOLEAN(b);
    case VAL_NIL:
        return true;
    case VAL_NUMBER:
        return AS_NUMBER(a) == AS_NUMBER(b);
    case VAL_OBJ:
        return AS_OBJ(a) == AS_OBJ(b);
    default:
        return false;
    }
}

void init_value_array(ValueArray *arr)
{
    arr->capacity = 0;
    arr->count = 0;
    arr->values = NULL;
}

void free_value_array(ValueArray *arr)
{
    FREE_ARRAY(arr->values, uint8_t, arr->capacity);
    init_value_array(arr);
}

void append_to_value_array(ValueArray *arr, Value value)
{
    if (arr->capacity < arr->count + 1)
    {
        int old_capacity = arr->capacity;
        arr->capacity = GROW_CAPACITY(old_capacity);
        arr->values = GROW_ARRAY(arr->values, Value, old_capacity, arr->capacity);
    }

    arr->values[arr->count++] = value;
}

void print_value(Value value)
{
    switch (value.type)
    {
    case VAL_BOOLEAN:
        printf(AS_BOOLEAN(value) ? "true" : "false");
        break;
    case VAL_NIL:
        printf("nil");
        break;
    case VAL_NUMBER:
        printf("%g", AS_NUMBER(value));
        break;
    case VAL_OBJ:
        print_obj(value);
        break;
    }
}
