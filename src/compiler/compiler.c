#include "compiler.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#include "object.h"
#include "scanner.h"
#include "value.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
  Scanner* scanner;

  bool panicMode;
  bool hadError;

  Token current;
  Token previous;
} Parser;

// The compiler is single-pass and always compiles exactly one source at a
// time, so the parser and scanner live in file scope instead of being passed
// down through every function (same pattern as Crafting Interpreters).
static Scanner scanner;
static Parser parser;
Compiler* current = NULL;

static Chunk* currentChunk() { return &current->function->chunk; }

static void initCompiler(Compiler* compiler, FunctionType type) {
  compiler->function = NULL;
  compiler->type = type;
  compiler->localCount = 0;
  compiler->scopeDepth = 0;
  compiler->function = newFunction();
  current = compiler;

  Local* local = &current->locals[current->localCount++];
  local->depth = 0;
  local->name.start = "";
  local->name.length = 0;
}

static void errorAt(Token* token, const char* message, va_list args) {
  fflush(stdout);

  if (parser.panicMode) return;

  parser.panicMode = true;

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

  parser.hadError = true;
}

static void errorAtCurrent(const char* message, ...) {
  va_list args;
  va_start(args, message);
  errorAt(&parser.current, message, args);
  va_end(args);
}

static void error(const char* message) {
  errorAt(&parser.previous, message, NULL);
}

static void advance() {
  parser.previous = parser.current;

  while (true) {
    parser.current = scanToken(&scanner);
    if (parser.current.type != TOKEN_ERROR) break;
    errorAtCurrent(parser.current.start);
  }
}

static void consume(TokenType type, const char* message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  errorAtCurrent(message);
}

static bool check(TokenType type) { return parser.current.type == type; }

static bool match(TokenType type) {
  if (!check(type)) return false;
  advance();
  return true;
}

// Newlines separate statements, but they are insignificant in a few places
// such as between `}` and `else`. This skips any that are present.
static void skipNewlines() {
  while (match(TOKEN_NEWLINE)) {
  }
}

static void emitByte(uint8_t byte) {
  writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
  emitByte(byte1);
  emitByte(byte2);
}

static void emitLoop(int loopStart) {
  emitByte(OP_LOOP);

  int offset = currentChunk()->count - loopStart + 2;
  if (offset > UINT16_MAX) error("Loop body too large.");

  emitByte((offset >> 8) & 0xff);
  emitByte(offset & 0xff);
}

static int emitJump(uint8_t instruction) {
  emitByte(instruction);
  emitByte(0xff);
  emitByte(0xff);
  return currentChunk()->count - 2;
}

static void emitReturn() { emitByte(OP_RETURN); }

static uint8_t makeConstant(Value value) {
  int constant = addConstant(currentChunk(), value);
  if (constant > UINT8_MAX) {
    error("Too many constants in one chunk.");
    return 0;
  }

  return (uint8_t)constant;
}

static void emitConstant(Value value) {
  emitBytes(OP_CONSTANT, makeConstant(value));
}

static void patchJump(int offset) {
  // -2 to adjust for the bytecode for the jump offset itself.
  int jump = currentChunk()->count - offset - 2;

  if (jump > UINT16_MAX) {
    error("Too much code to jump over.");
  }

  currentChunk()->code[offset] = (jump >> 8) & 0xff;
  currentChunk()->code[offset + 1] = jump & 0xff;
}

static ObjFunction* endCompiler() {
  emitReturn();
  ObjFunction* function = current->function;
#ifdef DEBUG_PRINT_CODE
  if (!parser.hadError) {
    disassembleChunk(currentChunk(), function->name != NULL
                                         ? function->name->chars
                                         : "<script>");
  }
#endif
  return function;
}

static void beginScope() { current->scopeDepth++; }
static void endScope() {
  current->scopeDepth--;
  while (current->localCount > 0 &&
         current->locals[current->localCount - 1].depth > current->scopeDepth) {
    emitByte(OP_POP);
    current->localCount--;
  }
}

// forward declaration
static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);
// end forward declaration

static uint8_t identifierConstant(Token* name) {
  return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static bool identifiersEqual(Token* a, Token* b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler* compiler, Token* name) {
  for (int i = compiler->localCount - 1; i >= 0; i--) {
    Local* local = &compiler->locals[i];
    if (identifiersEqual(name, &local->name)) {
      if (local->depth == -1) {
        error("Can't read local variable in its own initializer.");
      }
      return i;
    }
  }

  return -1;
}

static void addLocal(Token name) {
  if (current->localCount == UINT8_COUNT) {
    error("Too many local variables in function.");
    return;
  }
  Local* local = &current->locals[current->localCount++];
  local->name = name;
  local->depth = -1;
}

static void declareVariable() {
  if (current->scopeDepth == 0) return;

  Token* name = &parser.previous;
  for (int i = current->localCount - 1; i >= 0; i--) {
    Local* local = &current->locals[i];
    if (local->depth != -1 && local->depth < current->scopeDepth) {
      break;
    }

    if (identifiersEqual(name, &local->name)) {
      error("Already a variable with this name in this scope.");
    }
  }
  addLocal(*name);
}

static uint8_t parseVariable(const char* errorMessage) {
  consume(TOKEN_IDENTIFIER, errorMessage);

  declareVariable();
  if (current->scopeDepth > 0) return 0;

  return identifierConstant(&parser.previous);
}

static void markInitialized() {
  current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static void defineVariable(uint8_t global) {
  if (current->scopeDepth > 0) {
    markInitialized();
    return;
  }

  emitBytes(OP_DEFINE_GLOBAL, global);
}

static void and_(bool canAssign) {
  int endJump = emitJump(OP_JUMP_IF_FALSE);

  emitByte(OP_POP);
  parsePrecedence(PREC_AND);

  patchJump(endJump);
}

static void binary(bool canAssign) {
  TokenType operatorType = parser.previous.type;

  ParseRule* rule = getRule(operatorType);
  parsePrecedence((Precedence)(rule->precedence + 1));

  switch (operatorType) {
    case TOKEN_BANG_EQUAL:
      emitBytes(OP_EQUAL, OP_NOT);
      break;
    case TOKEN_EQUAL_EQUAL:
      emitByte(OP_EQUAL);
      break;
    case TOKEN_GREATER_THAN:
      emitByte(OP_GREATER);
      break;
    case TOKEN_GREATER_EQUAL:
      emitBytes(OP_LESS, OP_NOT);
      break;
    case TOKEN_LESS_THAN:
      emitByte(OP_LESS);
      break;
    case TOKEN_LESS_EQUAL:
      emitBytes(OP_GREATER, OP_NOT);
      break;
    case TOKEN_PLUS:
      emitByte(OP_ADD);
      break;
    case TOKEN_MINUS:
      emitByte(OP_SUBTRACT);
      break;
    case TOKEN_STAR:
      emitByte(OP_MULTIPLY);
      break;
    case TOKEN_SLASH:
      emitByte(OP_DIVIDE);
      break;
    default:
      return;
  }
}

static void literal(bool canAssign) {
  switch (parser.previous.type) {
    case TOKEN_TRUE:
      emitByte(OP_TRUE);
      break;
    case TOKEN_FALSE:
      emitByte(OP_FALSE);
      break;
    case TOKEN_NIL:
      emitByte(OP_NIL);
      break;
    default:
      return;
  }
}

static void grouping(bool canAssign) {
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(bool canAssign) {
  double value = strtod(parser.previous.start, NULL);
  emitConstant(NUMBER_VAL(value));
}

static void or_(bool canAssign) {
  int elseJump = emitJump(OP_JUMP_IF_FALSE);
  int endJump = emitJump(OP_JUMP);

  patchJump(elseJump);
  emitByte(OP_POP);

  parsePrecedence(PREC_OR);
  patchJump(endJump);
}

static void string(bool canAssign) {
  // copy from lexeme and removes the quotes from both ends
  emitConstant(OBJ_VAL(
      copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

static bool isCompoundAssignment(TokenType type) {
  switch (type) {
    case TOKEN_PLUS_EQUAL:
    case TOKEN_MINUS_EQUAL:
    case TOKEN_STAR_EQUAL:
    case TOKEN_SLASH_EQUAL:
      return true;
    default:
      return false;
  }
}

static void emitCompoundOp(TokenType type) {
  switch (type) {
    case TOKEN_PLUS_EQUAL:
      emitByte(OP_ADD);
      break;
    case TOKEN_MINUS_EQUAL:
      emitByte(OP_SUBTRACT);
      break;
    case TOKEN_STAR_EQUAL:
      emitByte(OP_MULTIPLY);
      break;
    case TOKEN_SLASH_EQUAL:
      emitByte(OP_DIVIDE);
      break;
    default:
      return;
  }
}

static void namedVariable(Token name, bool canAssign) {
  uint8_t getOp, setOp;
  int arg = resolveLocal(current, &name);
  if (arg != -1) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  } else {
    arg = identifierConstant(&name);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitBytes(setOp, (uint8_t)arg);
  } else if (canAssign && isCompoundAssignment(parser.current.type)) {
    TokenType operatorType = parser.current.type;
    advance();

    // `a += b` is compiled as `a = a + b`.
    emitBytes(getOp, (uint8_t)arg);
    expression();
    emitCompoundOp(operatorType);
    emitBytes(setOp, (uint8_t)arg);
  } else {
    emitBytes(getOp, (uint8_t)arg);
  }
}

static void variable(bool canAssign) {
  namedVariable(parser.previous, canAssign);
}

static void unary(bool canAssign) {
  TokenType operatorType = parser.previous.type;

  // compile the operand
  parsePrecedence(PREC_UNARY);

  switch (operatorType) {
    case TOKEN_MINUS:
      emitByte(OP_NEGATE);
      break;
    case TOKEN_BANG:
      emitByte(OP_NOT);
      break;

    default:
      return;
  }
}

static void expression() { parsePrecedence(PREC_ASSIGNMENT); }

// Statement terminator: ';' or newline. EOF is also accepted, which lets the
// final statement of a source omit its terminator.
static void terminator(const char* message) {
  if (match(TOKEN_SEMICOLON) || match(TOKEN_NEWLINE)) return;
  if (check(TOKEN_EOF)) return;

  consume(TOKEN_NEWLINE, message);
}

static void block() {
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration();
  }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void varDeclaration() {
  uint8_t global = parseVariable("Expect variable name.");
  if (match(TOKEN_EQUAL)) {
    expression();
  } else {
    emitByte(OP_NIL);
  }
  terminator("Expect newline or ';' after variable declaration.");

  defineVariable(global);
}

static void printStatement() {
  expression();
  terminator("Expect newline or ';' after value.");
  emitByte(OP_PRINT);
}

static void whileStatement() {
  int loopStart = currentChunk()->count;

  // Go-style condition: no parentheses, the body's `{` ends the expression.
  expression();

  int exitJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  statement();
  emitLoop(loopStart);

  patchJump(exitJump);
  emitByte(OP_POP);
}

static void synchronize() {
  parser.panicMode = false;

  while (parser.current.type != TOKEN_EOF) {
    if (parser.previous.type == TOKEN_SEMICOLON) return;
    switch (parser.current.type) {
      case TOKEN_CLASS:
      case TOKEN_FUNC:
      case TOKEN_LET:
      case TOKEN_FOR:
      case TOKEN_IF:
      case TOKEN_WHILE:
      case TOKEN_PRINT:
      case TOKEN_RETURN:
        return;

      default:;  // Do nothing.
    }

    advance();
  }
}

static void expressionStatement() {
  expression();
  terminator("Expect newline or ';' after expression.");
  emitByte(OP_POP);
}

static void forStatement() {
  beginScope();

  // Go-style header: `for init; condition; post { ... }`. There are no
  // parentheses, and the initializer declares the loop variable implicitly:
  // `for i = 0; ...` instead of `for let i = 0; ...`.
  if (match(TOKEN_SEMICOLON)) {
    // No initializer.
  } else {
    match(TOKEN_LET);  // Optional; usually omitted in `for` headers.

    uint8_t global = parseVariable("Expect variable name.");
    if (match(TOKEN_EQUAL)) {
      expression();
    } else {
      emitByte(OP_NIL);
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after loop initializer.");
    defineVariable(global);
  }

  // The header may be split across lines.
  skipNewlines();

  int loopStart = currentChunk()->count;
  int exitJump = -1;
  if (!match(TOKEN_SEMICOLON)) {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

    // Jump out of the loop if the condition is false.
    exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);  // Condition.
  }
  skipNewlines();

  // The post clause runs after the body but before the condition is checked
  // again. Jump over it on the first pass through the loop.
  if (!check(TOKEN_LEFT_BRACE)) {
    int bodyJump = emitJump(OP_JUMP);
    int incrementStart = currentChunk()->count;
    expression();
    emitByte(OP_POP);

    emitLoop(loopStart);
    loopStart = incrementStart;
    patchJump(bodyJump);
  }

  skipNewlines();
  statement();
  emitLoop(loopStart);
  if (exitJump != -1) {
    patchJump(exitJump);
    emitByte(OP_POP);  // Condition.
  }

  endScope();
}

static void ifStatement() {
  // Go-style condition: no parentheses, the branch's `{` ends the expression.
  expression();

  int thenJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  statement();

  // Allow `}` and `else` to sit on separate lines.
  skipNewlines();

  int elseJump = emitJump(OP_JUMP);

  patchJump(thenJump);

  if (match(TOKEN_ELSE)) {
    skipNewlines();
    statement();
  }
  emitByte(OP_POP);
  patchJump(elseJump);
}

static void declaration() {
  // Skip blank lines (and any newline left over from a ';' statement).
  if (match(TOKEN_NEWLINE)) return;

  if (match(TOKEN_LET)) {
    varDeclaration();
  } else {
    statement();
  }

  if (parser.panicMode) {
    synchronize();
  }
}

static void statement() {
  if (match(TOKEN_PRINT)) {
    printStatement();
  } else if (match(TOKEN_IF)) {
    ifStatement();
  } else if (match(TOKEN_WHILE)) {
    whileStatement();
  } else if (match(TOKEN_FOR)) {
    forStatement();
  } else if (match(TOKEN_LEFT_BRACE)) {
    beginScope();
    block();
    endScope();
  } else {
    expressionStatement();
  }
}

static void parsePrecedence(Precedence precedence) {
  advance();
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;

  if (prefixRule == NULL) {
    errorAtCurrent("Expect expression.");
    return;
  }

  bool canAssign = precedence <= PREC_ASSIGNMENT;
  prefixRule(canAssign);

  while (precedence <= getRule(parser.current.type)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule(canAssign);
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    error("Invalid assignment target.");
  }
}

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN] = {grouping, NULL, PREC_NONE},  // (
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},     // )
    [TOKEN_LEFT_BRACKET] = {NULL, NULL, PREC_NONE},    // [
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},     // }
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},           // ,
    [TOKEN_COLON] = {NULL, NULL, PREC_NONE},           // :
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},       // ;
    [TOKEN_HASH] = {NULL, NULL, PREC_NONE},            // #
    [TOKEN_DOT] = {NULL, NULL, PREC_NONE},             // .
    [TOKEN_DOT_DOT] = {NULL, NULL, PREC_NONE},         // ..

    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},                // +
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},              // -
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},              // *
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},             // /
    [TOKEN_PERCENT] = {NULL, NULL, PREC_NONE},               // %
    [TOKEN_STAR_STAR] = {NULL, NULL, PREC_NONE},             // **
    [TOKEN_SLASH_SLASH] = {NULL, NULL, PREC_NONE},           // //
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},                 // =
    [TOKEN_GREATER_THAN] = {NULL, binary, PREC_COMPARISON},  // >
    [TOKEN_LESS_THAN] = {NULL, binary, PREC_COMPARISON},     // <
    [TOKEN_BANG] = {unary, NULL, PREC_NONE},                 // !

    [TOKEN_TILDE] = {NULL, NULL, PREC_NONE},  // ~
    [TOKEN_PIPE] = {NULL, NULL, PREC_NONE},   // |
    [TOKEN_AMP] = {NULL, NULL, PREC_NONE},    // &
    [TOKEN_CARET] = {NULL, NULL, PREC_NONE},  // ^

    [TOKEN_PLUS_EQUAL] = {NULL, NULL, PREC_NONE},             // +=
    [TOKEN_MINUS_EQUAL] = {NULL, NULL, PREC_NONE},            // -=
    [TOKEN_STAR_EQUAL] = {NULL, NULL, PREC_NONE},             // *=
    [TOKEN_SLASH_EQUAL] = {NULL, NULL, PREC_NONE},            // /=
    [TOKEN_PERCENT_EQUAL] = {NULL, NULL, PREC_NONE},          // %=
    [TOKEN_STAR_STAR_EQUAL] = {NULL, NULL, PREC_NONE},        // **=
    [TOKEN_SLASH_SLASH_EQUAL] = {NULL, NULL, PREC_NONE},      // //=
    [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},      // ==
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},  // >=
    [TOKEN_LESS_EQUAL] = {NULL, binary, PREC_COMPARISON},     // <=
    [TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY},       // !=

    [TOKEN_TILDE_EQUAL] = {NULL, NULL, PREC_NONE},  // ~=
    [TOKEN_PIPE_EQUAL] = {NULL, NULL, PREC_NONE},   // |=
    [TOKEN_AMP_EQUAL] = {NULL, NULL, PREC_NONE},    // &=
    [TOKEN_CARET_EQUAL] = {NULL, NULL, PREC_NONE},  // ^=

    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},  // identifier
    [TOKEN_STRING] = {string, NULL, PREC_NONE},        // string
    [TOKEN_INTERPOLATION] = {NULL, NULL, PREC_NONE},   // string interpolation
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},        // number

    // Keywords.
    [TOKEN_AND] = {NULL, and_, PREC_AND},        // and
    [TOKEN_OR] = {NULL, or_, PREC_OR},           // or
    [TOKEN_NOT] = {NULL, NULL, PREC_NONE},       // not
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},    // nil
    [TOKEN_IN] = {NULL, NULL, PREC_NONE},        // in
    [TOKEN_IMPORT] = {NULL, NULL, PREC_NONE},    // import
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},     // class
    [TOKEN_IS] = {NULL, NULL, PREC_NONE},        // is
    [TOKEN_SUPER] = {NULL, NULL, PREC_NONE},     // super
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},        // if
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},      // else
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},   // true
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},  // false
    [TOKEN_FUNC] = {NULL, NULL, PREC_NONE},      // func
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},       // for
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},     // print
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},    // return
    [TOKEN_THIS] = {NULL, NULL, PREC_NONE},      // this
    [TOKEN_LET] = {NULL, NULL, PREC_NONE},       // let
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},     // while

    [TOKEN_NEWLINE] = {NULL, NULL, PREC_NONE},

    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
};

static ParseRule* getRule(TokenType type) { return &rules[type]; }

ObjFunction* compile(const char* source) {
  initScanner(&scanner, source);
  Compiler compiler;
  initCompiler(&compiler, TYPE_SCRIPT);

  parser.scanner = &scanner;
  parser.hadError = false;
  parser.panicMode = false;

  advance();
  while (!match(TOKEN_EOF)) {
    declaration();
  }
  endCompiler();

  ObjFunction* function = endCompiler();
  return parser.hadError ? NULL : function;
}
