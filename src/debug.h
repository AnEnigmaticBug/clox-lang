#ifndef CLOX_DEGUG_H
#define CLOX_DEBUG_H

#include "chunk.h"

void disassemble_chunk(Chunk *chunk, const char *name);
int disassemble_instr(Chunk *chunk, int offset);

#endif
