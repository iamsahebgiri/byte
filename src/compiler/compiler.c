#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

Chunk* compilingChunk;

static Chunk* currentChunk() {
  return compilingChunk;
}

static void errorAt(Parser* parser,
                    Token* token,
                    const char* message,
                    va_list args) {
  fflush(stdout);

  if (parser->panicMode)
    return;

  parser->panicMode = true;

  fprintf(stderr, "SyntaxError");

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {
    // do nothing
    // fprintf(stderr, " at %s", token->start);
  } else {
    if (token->length == 1 && *token->start == '\n') {
      fprintf(stderr, " at newline");
    } else {
      fprintf(stderr, " at '%.*s'", token->length, token->start);
    }
  }

  fprintf(stderr, ": ");
  vfprintf(stderr, message, args);
  fputs("\n", stderr);

  parser->hadError = true;
}

static void errorAtCurrent(Parser* parser, const char* message, ...) {
  va_list args;
  va_start(args, message);
  errorAt(parser, &parser->current, message, args);
  va_end(args);
}

static void error(Parser* parser, const char* message) {
  errorAt(parser, &parser->previous, message, NULL);
}

static void advance(Parser* parser) {
  parser->previous = parser->current;

  while (true) {
    parser->current = scanToken(parser->scanner);
    if (parser->current.type != TOKEN_EOF)
      break;
    errorAtCurrent(parser, parser->current.start);
  }
}

static void consume(Parser* parser, TokenType type, const char* message) {
  if (parser->current.type == type) {
    advance(parser);
    return;
  }

  errorAtCurrent(parser, message);
}

void emitByte(Parser* parser, uint8_t byte) {
  writeChunk(currentChunk(), byte, parser->previous.line);
}

void emitBytes(Parser* parser, uint8_t byte1, uint8_t byte2) {
  emitByte(parser, byte1);
  emitByte(parser, byte2);
}

static void emitReturn(Parser* parser) {
  emitByte(parser, OP_RETURN);
}

static uint8_t makeConstant(Parser* parser, Value value) {
  int constant = addConstant(currentChunk(), value);
  if (constant > UINT8_MAX) {
    error(parser, "Too many constants in one chunk.");
    return 0;
  }

  return (uint8_t)constant;
}

static void endCompiler(Parser* parser) {
  emitReturn(parser);
}

// forward declaration
static void expression();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Parser* parser, Precedence precedence);
// end forward declaration

static void binary(Parser* parser) {
  TokenType operatorType = parser->previous.type;

  ParseRule* rule = getRule(operatorType);
  parsePrecedence(parser, (Precedence)(rule->precedence + 1));

  switch (operatorType) {
    case TOKEN_PLUS:
      emitByte(parser, OP_ADD);
      break;
    case TOKEN_MINUS:
      emitByte(parser, OP_SUBTRACT);
      break;
    case TOKEN_STAR:
      emitByte(parser, OP_MULTIPLY);
      break;
    case TOKEN_SLASH:
      emitByte(parser, OP_DIVIDE);
      break;
    default:
      return;
  }
}

static void grouping(Parser* parser) {
  expression(parser);
  consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void emitConstant(Parser* parser, Value value) {
  emitBytes(parser, OP_CONSTANT, makeConstant(parser, value));
}

static void number(Parser* parser) {
  double value = strtod(parser->previous.start, NULL);
  emitConstant(parser, value);
}

static void unary(Parser* parser) {
  TokenType operatorType = parser->previous.type;

  // compile the operand
  parsePrecedence(parser, PREC_UNARY);

  switch (operatorType) {
    case TOKEN_MINUS:
      emitByte(parser, OP_NEGATE);
      break;

    default:
      return;
  }
}

static void parsePrecedence(Parser* parser, Precedence precedence) {
  advance(parser);
  ParseFn prefixRule = getRule(parser->previous.type)->prefix;

  if (prefixRule == NULL) {
    errorAtCurrent(parser, "Expect expression.");
    return;
  }
  prefixRule(parser);

  while (precedence <= getRule(parser->current.type)->precedence) {
    advance(parser);
    ParseFn infixRule = getRule(parser->previous.type)->infix;
    infixRule(parser);
  }
}

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN] = {grouping, NULL, PREC_NONE},  // (
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},     // )
    [TOKEN_LEFT_BRACKET] = {NULL, NULL, PREC_NONE},    // [
    [TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_NONE},   // ]
    [TOKEN_LEFT_BRACE] = {NULL, NULL, PREC_NONE},      // {
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},     // }
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},           // ,
    [TOKEN_COLON] = {NULL, NULL, PREC_NONE},           // :
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},       // ;
    [TOKEN_HASH] = {NULL, NULL, PREC_NONE},            // #
    [TOKEN_DOT] = {NULL, NULL, PREC_NONE},             // .
    [TOKEN_DOT_DOT] = {NULL, NULL, PREC_NONE},         // ..

    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},        // +
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},      // -
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},      // *
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},     // /
    [TOKEN_PERCENT] = {NULL, NULL, PREC_NONE},       // %
    [TOKEN_STAR_STAR] = {NULL, NULL, PREC_NONE},     // **
    [TOKEN_SLASH_SLASH] = {NULL, NULL, PREC_NONE},   // //
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},         // =
    [TOKEN_GREATER_THAN] = {NULL, NULL, PREC_NONE},  // >
    [TOKEN_LESS_THAN] = {NULL, NULL, PREC_NONE},     // <
    [TOKEN_BANG] = {NULL, NULL, PREC_NONE},          // !

    [TOKEN_TILDE] = {NULL, NULL, PREC_NONE},  // ~
    [TOKEN_PIPE] = {NULL, NULL, PREC_NONE},   // |
    [TOKEN_AMP] = {NULL, NULL, PREC_NONE},    // &
    [TOKEN_CARET] = {NULL, NULL, PREC_NONE},  // ^

    [TOKEN_PLUS_EQUAL] = {NULL, NULL, PREC_NONE},         // +=
    [TOKEN_MINUS_EQUAL] = {NULL, NULL, PREC_NONE},        // -=
    [TOKEN_STAR_EQUAL] = {NULL, NULL, PREC_NONE},         // *=
    [TOKEN_SLASH_EQUAL] = {NULL, NULL, PREC_NONE},        // /=
    [TOKEN_PERCENT_EQUAL] = {NULL, NULL, PREC_NONE},      // %=
    [TOKEN_STAR_STAR_EQUAL] = {NULL, NULL, PREC_NONE},    // **=
    [TOKEN_SLASH_SLASH_EQUAL] = {NULL, NULL, PREC_NONE},  // //=
    [TOKEN_EQUAL_EQUAL] = {NULL, NULL, PREC_NONE},        // ==
    [TOKEN_GREATER_EQUAL] = {NULL, NULL, PREC_NONE},      // >=
    [TOKEN_LESS_EQUAL] = {NULL, NULL, PREC_NONE},         // <=
    [TOKEN_BANG_EQUAL] = {NULL, NULL, PREC_NONE},         // !=

    [TOKEN_TILDE_EQUAL] = {NULL, NULL, PREC_NONE},  // ~=
    [TOKEN_PIPE_EQUAL] = {NULL, NULL, PREC_NONE},   // |=
    [TOKEN_AMP_EQUAL] = {NULL, NULL, PREC_NONE},    // &=
    [TOKEN_CARET_EQUAL] = {NULL, NULL, PREC_NONE},  // ^=

    [TOKEN_IDENTIFIER] = {NULL, NULL, PREC_NONE},     // identifier
    [TOKEN_STRING] = {NULL, NULL, PREC_NONE},         // string
    [TOKEN_INTERPOLATION] = {NULL, NULL, PREC_NONE},  // string interpolation
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},       // number

    // Keywords.
    [TOKEN_AND] = {NULL, NULL, PREC_NONE},     // and
    [TOKEN_OR] = {NULL, NULL, PREC_NONE},      // or
    [TOKEN_NOT] = {NULL, NULL, PREC_NONE},     // not
    [TOKEN_NIL] = {NULL, NULL, PREC_NONE},     // nil
    [TOKEN_IN] = {NULL, NULL, PREC_NONE},      // in
    [TOKEN_IMPORT] = {NULL, NULL, PREC_NONE},  // import
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},   // class
    [TOKEN_IS] = {NULL, NULL, PREC_NONE},      // is
    [TOKEN_SUPER] = {NULL, NULL, PREC_NONE},   // super
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},      // if
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},    // else
    [TOKEN_TRUE] = {NULL, NULL, PREC_NONE},    // true
    [TOKEN_FALSE] = {NULL, NULL, PREC_NONE},   // false
    [TOKEN_FN] = {NULL, NULL, PREC_NONE},      // fn
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},     // for
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},   // print
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},  // return
    [TOKEN_THIS] = {NULL, NULL, PREC_NONE},    // this
    [TOKEN_LET] = {NULL, NULL, PREC_NONE},     // let
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},   // while

    [TOKEN_NEWLINE] = {NULL, NULL, PREC_NONE},

    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
};

static ParseRule* getRule(TokenType type) {
  return &rules[type];
}

static void expression(Parser* parser) {
  parsePrecedence(parser, PREC_ASSIGNMENT);
}

bool compile(const char* source, Chunk* chunk) {
  Parser parser;
  Scanner scanner;
  initScanner(&scanner, source);
  compilingChunk = chunk;

  parser.scanner = &scanner;
  parser.hadError = false;
  parser.panicMode = false;

  advance(&parser);
  expression(&parser);

  endCompiler(&parser);

#ifdef DEBUG_PRINT_CODE
  if (!parser.hadError) {
    disassembleChunk(currentChunk(), "code");
  }
#endif
  return !parser.hadError;
}