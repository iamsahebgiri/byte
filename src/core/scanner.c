#include "scanner.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void initScanner(Scanner* s, const char* source) {
  s->current = source;
  s->start = source;
  s->line = 1;
  s->interpolatingCount = -1;
}

bool isAtEnd(Scanner* s) {
  return *s->current == '\0';
}

static Token makeToken(Scanner* s, TokenType type) {
  Token t;
  t.type = type;
  t.start = s->start;
  t.length = (int)(s->current - s->start);
  t.line = s->line;
  return t;
}

static Token errorToken(Scanner* s, const char* message, ...) {
  va_list args;
  va_start(args, message);
  char* err = NULL;
  int length = vasprintf(&err, message, args);
  va_end(args);

  Token t;
  t.type = TOKEN_ERROR;
  t.start = err;
  if (err != NULL) {
    t.length = length;
  } else {
    t.length = 0;
  }
  t.line = s->line;
  return t;
}

static bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

static bool isBinary(char c) {
  return c == '0' || c == '1';
}

static bool isAlpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool isOctal(char c) {
  return c >= '0' && c <= '7';
}

static bool isHex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

static char advance(Scanner* s) {
  s->current++;
  if (s->current[-1] == '\n')
    s->line++;
  return s->current[-1];
}

static bool match(Scanner* s, char expected) {
  if (isAtEnd(s))
    return false;
  if (*s->current != expected)
    return false;

  s->current++;
  if (s->current[-1] == '\n')
    s->line++;
  return true;
}

static char current(Scanner* s) {
  return *s->current;
}

static char previous(Scanner* s) {
  return s->current[-1];
}

static char next(Scanner* s) {
  if (isAtEnd(s))
    return '\0';
  return s->current[1];
}

static void skipWhitespace(Scanner* s) {
  while (true) {
    char c = current(s);

    switch (c) {
      case ' ':
      case '\r':
      case '\t':
        advance(s);
        break;

      // skip line comment
      case '#': {
        while (current(s) != '\n' && !isAtEnd(s))
          advance(s);
        break;
      }

      default:
        return;
    }
  }
}

static Token string(Scanner* s, char quote) {
  while (current(s) != quote && !isAtEnd(s)) {
    if (current(s) == '$' && next(s) == '{' && previous(s) != '\\') {
      if (s->interpolatingCount - 1 < MAX_INTERPOLATION_NESTING) {
        s->interpolatingCount++;
        s->interpolating[s->interpolatingCount] = (int)quote;
        s->current++;
        Token tkn = makeToken(s, TOKEN_INTERPOLATION);
        s->current++;
        return tkn;
      }

      return errorToken(s, "maximum interpolation nesting of %d exceeded by %d",
                        MAX_INTERPOLATION_NESTING,
                        MAX_INTERPOLATION_NESTING - s->interpolatingCount + 1);
    }
    if (current(s) == '\\' && (next(s) == quote || next(s) == '\\')) {
      advance(s);
    }
    advance(s);
  }

  if (isAtEnd(s))
    return errorToken(s, "unterminated string (opening quote not matched)");

  match(s, quote);  // the closing quote
  return makeToken(s, TOKEN_STRING);
}

Token number(Scanner* s) {
  while (isDigit(current(s)))
    advance(s);

  // See if it has a floating point. Make sure there is a digit after the "."
  // so we don't get confused by method calls on number literals.
  if (current(s) == '.' && isDigit(next(s))) {
    advance(s);
    while (isDigit(current(s)))
      advance(s);
  }

  // See if the number is in scientific notation.
  if (match(s, 'e') || match(s, 'E')) {
    // Allow a single positive/negative exponent symbol.
    if (!match(s, '+')) {
      match(s, '-');
    }

    if (!isDigit(current(s))) {
      return errorToken(s, "unterminated scientific notation");
    }

    while (isDigit(current(s)))
      advance(s);
  }

  return makeToken(s, TOKEN_NUMBER);
}

static Token identifier(Scanner* s) {
  while (isAlpha(current(s)) || isDigit(current(s)))
    advance(s);

  TokenType type = TOKEN_IDENTIFIER;
  size_t length = s->current - s->start;

  for (int i = 0; keywords[i].identifier != NULL; i++) {
    if (length == keywords[i].length &&
        memcmp(s->start, keywords[i].identifier, length) == 0) {
      type = keywords[i].tokenType;
      break;
    }
  }

  return makeToken(s, type);
}

Token scanToken(Scanner* s) {
  skipWhitespace(s);

  s->start = s->current;

  if (isAtEnd(s))
    return makeToken(s, TOKEN_EOF);

  char c = advance(s);

  if (isDigit(c))
    return number(s);
  else if (isAlpha(c))
    return identifier(s);

  switch (c) {
    case '(':
      return makeToken(s, TOKEN_LEFT_PAREN);
    case ')':
      return makeToken(s, TOKEN_RIGHT_PAREN);
    case '[':
      return makeToken(s, TOKEN_LEFT_BRACKET);
    case ']':
      return makeToken(s, TOKEN_RIGHT_BRACKET);
    case '{':
      return makeToken(s, TOKEN_LEFT_BRACE);
    case '}':
      if (s->interpolatingCount > -1) {
        Token token = string(s, (char)s->interpolating[s->interpolatingCount]);
        s->interpolatingCount--;
        return token;
      }
      return makeToken(s, TOKEN_RIGHT_BRACE);
    case ',':
      return makeToken(s, TOKEN_COMMA);
    case ':':
      return makeToken(s, TOKEN_COLON);
    case '.':
      return makeToken(s, match(s, '.') ? TOKEN_DOT_DOT : TOKEN_DOT);
    case '+':
      return makeToken(s, match(s, '=') ? TOKEN_PLUS_EQUAL : TOKEN_PLUS);
    case '-':
      return makeToken(s, match(s, '=') ? TOKEN_MINUS_EQUAL : TOKEN_MINUS);
    case '*': {
      if (match(s, '*')) {
        return makeToken(
            s, match(s, '=') ? TOKEN_STAR_STAR_EQUAL : TOKEN_STAR_STAR);
      }
      return makeToken(s, match(s, '=') ? TOKEN_STAR_EQUAL : TOKEN_STAR);
    }
    case '/': {
      if (match(s, '/')) {
        return makeToken(
            s, match(s, '=') ? TOKEN_SLASH_SLASH_EQUAL : TOKEN_SLASH_SLASH);
      }
      return makeToken(s, match(s, '=') ? TOKEN_SLASH_EQUAL : TOKEN_SLASH);
    }
    case '%':
      return makeToken(s, match(s, '=') ? TOKEN_PERCENT_EQUAL : TOKEN_PERCENT);
    case '=':
      return makeToken(s, match(s, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
    case '>':
      return makeToken(
          s, match(s, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER_THAN);
    case '<':
      return makeToken(s, match(s, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS_THAN);
    case '!':
      return makeToken(s, match(s, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
    case '~':
      return makeToken(s, match(s, '=') ? TOKEN_TILDE_EQUAL : TOKEN_TILDE);
    case '|':
      return makeToken(s, match(s, '=') ? TOKEN_PIPE_EQUAL : TOKEN_PIPE);
    case '&':
      return makeToken(s, match(s, '=') ? TOKEN_AMP_EQUAL : TOKEN_AMP);
    case '^':
      return makeToken(s, match(s, '=') ? TOKEN_CARET_EQUAL : TOKEN_CARET);
    case '\n':
      return makeToken(s, TOKEN_NEWLINE);
    case '"':
      return string(s, '"');
    case '\'':
      return string(s, '\'');

    default:
      break;
  }

  return errorToken(s, "unexpected character %c", c);
}