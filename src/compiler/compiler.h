#ifndef BYTE_COMPILER_H
#define BYTE_COMPILER_H

#include <stdarg.h>

#include "chunk.h"
#include "common.h"
#include "scanner.h"

typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,  // =, &=, |=, *=, +=, -=, /=, **=, %=, ^=, //=, ~=
  PREC_OR,          // or
  PREC_AND,         // and
  PREC_EQUALITY,    // ==, !=
  PREC_COMPARISON,  // <, >, <=, >=
  PREC_BIT_OR,      // |
  PREC_BIT_XOR,     // ^
  PREC_BIT_AND,     // &
  PREC_RANGE,       // ..
  PREC_TERM,        // +, -
  PREC_FACTOR,      // *, /, %, **, //
  PREC_UNARY,       // !, -, ~,
  PREC_CALL,        // ., ()
  PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

typedef struct {
  Token name;
  int depth;
} Local;

typedef struct {
  Local locals[UINT8_COUNT];
  int localCount;
  int scopeDepth;
} Compiler;

bool compile(const char* source, Chunk* chunk);

#endif
