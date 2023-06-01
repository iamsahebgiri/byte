#ifndef BYTE_SCANNER_H
#define BYTE_SCANNER_H

#include "common.h"

typedef enum {
  TOKEN_LEFT_PAREN,     // (
  TOKEN_RIGHT_PAREN,    // )
  TOKEN_LEFT_BRACKET,   // [
  TOKEN_RIGHT_BRACKET,  // ]
  TOKEN_LEFT_BRACE,     // {
  TOKEN_RIGHT_BRACE,    // }
  TOKEN_COMMA,          // ,
  TOKEN_COLON,          // :
  TOKEN_SEMICOLON,      // ;
  TOKEN_HASH,           // #
  TOKEN_DOT,            // .
  TOKEN_DOT_DOT,        // ..

  TOKEN_PLUS,          // +
  TOKEN_MINUS,         // -
  TOKEN_STAR,          // *
  TOKEN_SLASH,         // /
  TOKEN_PERCENT,       // %
  TOKEN_STAR_STAR,     // **
  TOKEN_SLASH_SLASH,   // //
  TOKEN_EQUAL,         // =
  TOKEN_GREATER_THAN,  // >
  TOKEN_LESS_THAN,     // <
  TOKEN_BANG,          // !

  TOKEN_TILDE,  // ~
  TOKEN_PIPE,   // |
  TOKEN_AMP,    // &
  TOKEN_CARET,  // ^

  TOKEN_PLUS_EQUAL,         // +=
  TOKEN_MINUS_EQUAL,        // -=
  TOKEN_STAR_EQUAL,         // *=
  TOKEN_SLASH_EQUAL,        // /=
  TOKEN_PERCENT_EQUAL,      // %=
  TOKEN_STAR_STAR_EQUAL,    // **=
  TOKEN_SLASH_SLASH_EQUAL,  // //=
  TOKEN_EQUAL_EQUAL,        // ==
  TOKEN_GREATER_EQUAL,      // >=
  TOKEN_LESS_EQUAL,         // <=
  TOKEN_BANG_EQUAL,         // !=

  TOKEN_TILDE_EQUAL,  // ~=
  TOKEN_PIPE_EQUAL,   // |=
  TOKEN_AMP_EQUAL,    // &=
  TOKEN_CARET_EQUAL,  // ^=

  TOKEN_IDENTIFIER,  // identifier
  TOKEN_STRING,      // string
  /*
  A portion of a string literal preceding an interpolated
  expression. This
  string:
      "a ${b} c ${d} e"
  is tokenized to:
      TOKEN_INTERPOLATION "a "
      TOKEN_IDENTIFIER    b
      TOKEN_INTERPOLATION " c "
      TOKEN_IDENTIFIER    d
      TOKEN_STRING        " e"
  */
  TOKEN_INTERPOLATION,  // string interpolation
  TOKEN_NUMBER,         // number

  // Keywords.
  TOKEN_AND,     // and
  TOKEN_OR,      // or
  TOKEN_NOT,     // not
  TOKEN_NIL,     // nil
  TOKEN_IN,      // in
  TOKEN_IMPORT,  // import
  TOKEN_CLASS,   // class
  TOKEN_IS,      // is
  TOKEN_SUPER,   // super
  TOKEN_IF,      // if
  TOKEN_ELSE,    // else
  TOKEN_TRUE,    // true
  TOKEN_FALSE,   // false
  TOKEN_FN,      // fn
  TOKEN_FOR,     // for
  TOKEN_PRINT,   // print
  TOKEN_RETURN,  // return
  TOKEN_THIS,    // this
  TOKEN_LET,     // let
  TOKEN_WHILE,   // while

  TOKEN_NEWLINE,

  TOKEN_EOF,
  TOKEN_ERROR,
} TokenType;

typedef struct {
  const char* identifier;
  size_t length;
  TokenType tokenType;
} Keyword;

// The table of reserved words and their associated token types.
static Keyword keywords[] = {
    {"and", 3, TOKEN_AND},       {"or", 2, TOKEN_OR},
    {"not", 3, TOKEN_NOT},       {"nil", 3, TOKEN_NIL},
    {"in", 2, TOKEN_IN},         {"is", 2, TOKEN_IS},
    {"import", 6, TOKEN_IMPORT}, {"class", 5, TOKEN_CLASS},
    {"if", 2, TOKEN_IF},         {"else", 4, TOKEN_ELSE},
    {"true", 4, TOKEN_TRUE},     {"false", 5, TOKEN_FALSE},
    {"fn", 2, TOKEN_FN},         {"for", 3, TOKEN_FOR},
    {"print", 5, TOKEN_PRINT},   {"return", 6, TOKEN_RETURN},
    {"super", 5, TOKEN_SUPER},   {"this", 4, TOKEN_THIS},
    {"let", 3, TOKEN_LET},       {"while", 5, TOKEN_WHILE},
    {NULL, 0, TOKEN_EOF}  // Sentinel to mark the end of the array.
};

typedef struct {
  TokenType type;
  const char* start;
  int length;
  int line;
} Token;

typedef struct {
  const char* start;
  const char* current;
  int line;
  int interpolatingCount;
  int interpolating[MAX_INTERPOLATION_NESTING];
} Scanner;

void initScanner(Scanner* s, const char* source);
Token scanToken(Scanner* s);
bool isAtEnd(Scanner* s);

#endif