#ifndef BYTE_COMPILER_H
#define BYTE_COMPILER_H

#include <stdarg.h>
#include "chunk.h"
#include "scanner.h"
#include "common.h"

typedef struct {
  Scanner* scanner;

  bool panicMode;
  bool hadError;

  Token current;
  Token previous;
} Parser;

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

typedef void (*ParseFn)();

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

bool compile(const char* source, Chunk* chunk);

#endif