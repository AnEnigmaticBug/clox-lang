#include "chunk.h"
#include "memory.h"
#include "vm.h"

void init_chunk(Chunk *chunk)
{
    chunk->capacity = 0;
    chunk->count = 0;
    chunk->code = NULL;
    chunk->line_nos = NULL;
    init_value_array(&chunk->constants);
}

void free_chunk(Chunk *chunk)
{
    FREE_ARRAY(chunk->code, uint8_t, chunk->capacity);
    FREE_ARRAY(chunk->line_nos, int, chunk->capacity);
    free_value_array(&chunk->constants);
    init_chunk(chunk);
}

void append_to_chunk(Chunk *chunk, uint8_t byte, int line_no)
{
    if (chunk->capacity < chunk->count + 1)
    {
        int old_capacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(old_capacity);
        chunk->code = GROW_ARRAY(chunk->code, uint8_t, old_capacity, chunk->capacity);
        chunk->line_nos = GROW_ARRAY(chunk->line_nos, int, old_capacity, chunk->capacity);
    }

    chunk->code[chunk->count] = byte;
    chunk->line_nos[chunk->count] = line_no;
    ++chunk->count;
}

int add_constant(Chunk *chunk, Value value)
{
    push(value);
    append_to_value_array(&chunk->constants, value);
    pop();
    return chunk->constants.count - 1;
}
