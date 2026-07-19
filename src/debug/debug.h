#ifndef BYTE_DEBUG_H
#define BYTE_DEBUG_H

#include <stdio.h>
#include "core/chunk.h"

void disassembleChunk(Chunk* chunk, const char* name);
int disassembleInstruction(Chunk* chunk, int offset);

#endif
