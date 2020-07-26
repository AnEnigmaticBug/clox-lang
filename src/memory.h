#ifndef CLOX_MEMORY_H
#define CLOX_MEMORY_H

#include "common.h"
#include "obj.h"

#define ALLOCATE(type, count) \
    (type *)reallocate(NULL, 0, sizeof(type) * (count))

#define FREE(ptr, type) reallocate(ptr, sizeof(type), 0)

#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity)*2)

#define GROW_ARRAY(arr, type, old_capacity, new_capacity) \
    (type *)reallocate(arr, sizeof(type) * (old_capacity), sizeof(type) * (new_capacity))

#define FREE_ARRAY(arr, type, old_capacity) \
    (type *)reallocate(arr, sizeof(type) * (old_capacity), 0)

void *reallocate(void *arr, size_t old_capacity, size_t new_capacity);
void mark_obj(Obj *obj);
void mark_value(Value value);
void collect_garbage();
void free_objs();

#endif
