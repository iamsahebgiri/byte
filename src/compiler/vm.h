#ifndef BYTE_VM_H
#define BYTE_VM_H

#include "core/chunk.h"
#include "core/value.h"
#include "table.h"

#define STACK_MAX 256

typedef struct {
  Chunk* chunk;
  uint8_t* ip;
  Value stack[STACK_MAX];
  Value* stackTop;
  Table globals;  // global variables
  Table strings;  // pool for string interning
  Obj* objects;   // head of linked list for heap values
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();

InterpretResult interpret(const char* source);
void push(Value value);
Value pop();

#endif
