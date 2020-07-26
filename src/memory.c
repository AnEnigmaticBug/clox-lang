#include "memory.h"
#include <stdlib.h>
#include "compiler.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2

void *reallocate(void *arr, size_t old_capacity, size_t new_capacity)
{
    vm.bytes_allocated += new_capacity - old_capacity;

    if (new_capacity > old_capacity)
    {
#ifdef DEBUG_STRESS_GC
        collect_garbage();
#endif
    }

    if (vm.bytes_allocated > vm.next_gc)
    {
        collect_garbage();
    }

    if (new_capacity == 0)
    {
        free(arr);
        return NULL;
    }

    return realloc(arr, new_capacity);
}

void mark_obj(Obj *obj)
{
    if (obj == NULL || obj->is_marked)
    {
        return;
    }

#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void *)obj);
    print_value(OBJ_VAL(obj));
    printf("\n");
#endif

    obj->is_marked = true;

    if (vm.gray_capacity < vm.gray_count + 1)
    {
        vm.gray_capacity = GROW_CAPACITY(vm.gray_capacity);
        vm.gray_stack = realloc(vm.gray_stack, sizeof(Obj *) * vm.gray_capacity);
    }

    vm.gray_stack[vm.gray_count++] = obj;
}

void mark_value(Value value)
{
    if (!IS_OBJ(value))
    {
        return;
    }

    mark_obj(AS_OBJ(value));
}

static void mark_array(ValueArray *array)
{
    for (int i = 0; i < array->count; ++i)
    {
        mark_value(array->values[i]);
    }
}

static void blacken_obj(Obj *obj)
{
#ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void *)obj);
    print_value(OBJ_VAL(obj));
    printf("\n");
#endif
    switch (obj->type)
    {
    case OBJ_CLASS:
    {
        ObjClass *klass = (ObjClass *)obj;
        mark_obj((Obj *)klass->name);
        break;
    }
    case OBJ_CLOSURE:
    {
        ObjClosure *closure = (ObjClosure *)obj;
        mark_obj((Obj *)closure->function);
        for (int i = 0; i < closure->upvalue_count; ++i)
        {
            mark_obj((Obj *)closure->upvalues[i]);
        }
        break;
    }
    case OBJ_FUNCTION:
    {
        ObjFunction *function = (ObjFunction *)obj;
        mark_obj((Obj *)function->name);
        mark_array(&function->chunk.constants);
        break;
    }
    case OBJ_INSTANCE:
    {
        ObjInstance *instance = (ObjInstance *)obj;
        mark_obj((Obj *)instance->klass);
        mark_table(&instance->fields);
        break;
    }
    case OBJ_UPVALUE:
        mark_value(((ObjUpvalue *)obj)->closed);
        break;
    case OBJ_NATIVE:
    case OBJ_STRING:
        break;
    }
}

static void free_obj(Obj *obj)
{
    switch (obj->type)
    {
    case OBJ_CLASS:
        FREE(obj, ObjClass);
        break;
    case OBJ_CLOSURE:
        FREE(obj, ObjClosure);
        break;
    case OBJ_FUNCTION:
    {
        ObjFunction *function = (ObjFunction *)obj;
        free_chunk(&function->chunk);
        FREE(obj, ObjFunction);
        break;
    }
    case OBJ_INSTANCE:
    {
        ObjInstance *instance = (ObjInstance *)obj;
        free_table(&instance->fields);
        FREE(obj, ObjInstance);
        break;
    }
    case OBJ_NATIVE:
        FREE(obj, OBJ_NATIVE);
        break;
    case OBJ_STRING:
    {
        ObjString *string = (ObjString *)obj;
        FREE_ARRAY(string->chars, char, string->length + 1);
        FREE(obj, ObjString);
        break;
    }
    case OBJ_UPVALUE:
    {
        ObjClosure *closure = (ObjClosure *)obj;
        FREE_ARRAY(closure->upvalues, ObjUpvalue *, closure->upvalue_count);
        FREE(obj, OBJ_UPVALUE);
        break;
    }
    }

    free(vm.gray_stack);
}

static void mark_roots()
{
    for (Value *slot = vm.stack; slot < vm.stack_top; ++slot)
    {
        mark_value(*slot);
    }

    for (int i = 0; i < vm.frame_count; ++i)
    {
        mark_obj((Obj *)vm.frames[i].closure);
    }

    for (ObjUpvalue *upvalue = vm.open_upvalues; upvalue != NULL; upvalue = upvalue->next)
    {
        mark_obj((Obj *)upvalue);
    }

    mark_table(&vm.globals);
    mark_compiler_roots();
}

static void trace_references()
{
    while (vm.gray_count > 0)
    {
        Obj *obj = vm.gray_stack[--vm.gray_count];
        blacken_obj(obj);
    }
}

static void sweep()
{
    Obj *previous = NULL;
    Obj *obj = vm.objs;
    while (obj != NULL)
    {
        if (obj->is_marked)
        {
            obj->is_marked = false;
            previous = obj;
        }
        else
        {
            Obj *unreached = obj;
            obj = obj->next;

            if (previous != NULL)
            {
                previous->next = obj;
            }
            else
            {
                vm.objs = obj;
            }

            free_obj(unreached);
        }
    }
}

void collect_garbage()
{
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
    size_t before = vm.bytes_allocated;
#endif

    mark_roots();
    trace_references();
    table_remove_white(&vm.strings);
    sweep();

    vm.next_gc = vm.bytes_allocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
    printf("   collected %ld bytes (from %ld to %ld) next at %ld\n", before - vm.bytes_allocated, before, vm.bytes_allocated, vm.next_gc);
#endif
}

void free_objs()
{
#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void *)obj, obj->type);
#endif
    Obj *obj = vm.objs;
    while (obj != NULL)
    {
        Obj *next = obj->next;
        free_obj(obj);
        obj = next;
    }
}
