# Pratt Parsing

How expression parsing works in the compiler (`parsePrecedence` in
`src/compiler/compiler.c`), and why the table-driven approach is used instead
of a hand-written recursive-descent chain. The design comes from Vaughan
Pratt's 1973 paper _[Top Down Operator Precedence](https://tdop.sourceforge.net/)_,
in the concrete form used by
[Crafting Interpreters (Ch. 17, Compiling Expressions)](https://craftinginterpreters.com/compiling-expressions.html).

## The problem Pratt parsing solves

Plain recursive descent needs one function per precedence level
(`equality()` → `comparison()` → `term()` → `factor()` → `unary()` →
`primary()`), and every level's call chain must be kept in sync as operators
are added. Pratt parsing replaces the call chain with **a table and two
loops**, in one place:

1. Every token _knows_ statically:
   - what kind of expression it can **begin** (prefix parse function),
   - what kind of expression it can **continue** (infix parse function),
   - **how tightly it binds** (precedence).
2. `parsePrecedence` uses that information to greedily consume tokens,
   stopping only when the next infix operator binds too loosely to belong
   to the expression it is currently building.

## The table

The whole precedence scheme is one array indexed by token type; adding a new
operator to the language is usually one new line here plus a parse function:

```c
typedef struct {
  ParseFn prefix;         // can this token begin an expression?
  ParseFn infix;          // can this token continue one?
  Precedence precedence;  // how tightly does it bind?
} ParseRule;

ParseRule rules[] = {
  [TOKEN_PLUS]  = {NULL,   binary, PREC_TERM},
  [TOKEN_MINUS] = {unary,  binary, PREC_TERM},
  [TOKEN_STAR]  = {NULL,   binary, PREC_FACTOR},
  [TOKEN_SLASH] = {NULL,   binary, PREC_FACTOR},
  ...
};
```

## The precedence ladder

The `Precedence` enum (`src/compiler/compiler.h`) orders binding power from
loosest to tightest:

```
PREC_NONE
PREC_ASSIGNMENT    =, +=, ...         (see Assignment)
PREC_OR            or
PREC_AND           and
PREC_EQUALITY      == !=
PREC_COMPARISON    < > <= >=
PREC_BIT_OR        |
PREC_BIT_XOR       ^
PREC_BIT_AND       &
PREC_RANGE         ..
PREC_TERM          + -
PREC_FACTOR        * / % ** //
PREC_UNARY         ! - ~
PREC_CALL          . ()
PREC_PRIMARY
```

Two behaviors the ladder encodes:

- **Tighter binds first.** `1 + 2 * 3` parses as `1 + (2 * 3)` because `*`
  sits on a tighter tier than `+`, and the engine only continues while the
  _lookahead_ operator binds at least as tightly as the level it was asked
  for.
- **Same tier is left-associative for free.** `binary()` parses its right
  operand one tier _tighter_ than itself:
  `parsePrecedence(parser, rule->precedence + 1)`. So in `a + b + c`, the
  first `+`'s right operand stops before consuming another `+`, and the
  outer expression does the continuation — yielding `(a + b) + c`.

- **Prefix binding.** Unary functions pass `PREC_UNARY`, which lets their
  operand include every tighter tier (call, primary), but no loose one.
  Consequences: `- - 1` is `(-(-1))` (prefix is right-associative), and
  `-1 + 2` is `(-1) + 2`, since `+` is looser than `PREC_UNARY` and stops the
  operand loop.

## The engine

The parser state is a file-scope singleton in `src/compiler/compiler.c`, so
internal parse functions take no `Parser*` argument — they operate on the
global `parser` (and the token "cursor" inside it):

```c
static void parsePrecedence(Precedence precedence) {
  advance();                          // previous = a real token
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

static void expression() {
  parsePrecedence(PREC_ASSIGNMENT);   // loosest tier = full expression
}
```

Two details worth noticing:

- `advance()` runs **first**. The engine works purely through `previous`
  (the token just consumed) and `current` (the lookahead), so each parse
  function assumes `previous` holds the token it was invoked for.
- The stopping condition `precedence <= getRule(current)->precedence` is
  checked against the **lookahead token**, not the operator just handled.
  That check _is_ the algorithm.
- Every parse function also receives a `canAssign` flag. `=` has no entry in
  the infix table at all; the identifier's **prefix** rule consumes it
  directly (see below), and anything stuck next to an `=` that cannot be
  assigned to is caught by the trailing check → `Invalid assignment target.`

## Walkthrough

`1 + 2 * 3`, parsed via `expression()` (minimum = `PREC_ASSIGNMENT`):

| Step | previous   | current    | Action                                                                          |
| ---- | ---------- | ---------- | ------------------------------------------------------------------------------- |
| 1    | `NUMBER 1` | `+`        | `advance()`, `number()` emits `1`; `+` (`PREC_TERM`) ≥ min → infix loop enters  |
| 2    | `+`        | `NUMBER 2` | `binary()` parses its operand at `PREC_TERM + 1` = `PREC_FACTOR`                |
| 3    | `NUMBER 2` | `*`        | `number()` emits `2`; `*` (`PREC_FACTOR`) ≥ `PREC_FACTOR` → its own infix loop  |
| 4    | `*`        | `NUMBER 3` | `binary()` parses operand at `PREC_FACTOR + 1`; `number()` emits `3`            |
| 5    | `NUMBER 3` | EOF        | EOF is `PREC_NONE` < `PREC_FACTOR` → `*`'s operand loop stops → `2 * 3` emitted |
| 6    | `*`        | EOF        | back in step 1's loop: EOF < `PREC_TERM` → stops → `1 + (2 * 3)` emitted        |

## Why errors are cheap here

If `advance()` lands on a token with no prefix rule (an expression was
expected where there isn't one), `parsePrecedence` reports `Expect
expression.` and returns immediately — the owning statement's recovery
(`synchronize()`) takes over from there. There is no deep call stack to
unwind, because every precedence level is the _same_ function.

## Assignment (implemented, off the table)

`=` is an interesting case: it has **no row in the infix table at all**
(`TOKEN_EQUAL` sits at `PREC_NONE`). Assignment lives in the *prefix* rule
instead:

```c
static void namedVariable(Token name, bool canAssign) {
  uint8_t arg = identifierConstant(&name);
  if (canAssign && match(TOKEN_EQUAL)) {
    expression();                            // right-hand side
    emitBytes(OP_SET_GLOBAL, arg);
  } else {
    emitBytes(OP_GET_GLOBAL, arg);
  }
}
```

- `canAssign` is `precedence <= PREC_ASSIGNMENT`, so who exactly is allowed
  to look for `=` is decided by *where in the precedence climb* the prefix
  rule is being invoked from, not by the table.
- Because the right-hand side is parsed with its own `expression()` call
  (not through the infix loop), `a = b = 1` nests right-associatively.
- After the infix loop, prefixes that were not allowed to assign but are
  still facing `=` produce `Invalid assignment target.` `a * b = 2` is,
  correctly, a compile error, while `a = b * 2` is fine.

## Compound assignment

`+=`, `-=`, `*=` and `/=` are handled in `namedVariable()` too, alongside `=`.
After the target has been resolved, the operator compiles as a read, the
right-hand expression, the arithmetic opcode, and a write:

```
a += b   ->  GET a; <b>; ADD; SET a
```

The operator token has no infix-table row, so only a plain identifier target
is accepted, just like `=`.

## Known gaps / roadmap

The `PREC_*` ladder exists, but some tiers have no parse functions wired in yet
(their `rules[]` entries are `NULL`), so those constructs are currently
rejected by the parser:

- **`PREC_RANGE`** — `..` ranges.
- **`PREC_CALL`** — calls and member access (`.`).
- The remaining compound operators (`%=`, `**=`, bitwise assignments) are
  tokenized but not compiled.

Failing loudly (`Expect expression.`) is fine while the language is small,
but the ladder already sits in the order the grammar should grow into.

## Where to read more

- Vaughan Pratt, [Top Down Operator Precedence](https://tdop.sourceforge.net/)
- Bob Nystrom, [Compiling Expressions](https://craftinginterpreters.com/compiling-expressions.html) (Crafting Interpreters, Ch. 17)
- [Simple but Powerful Pratt Parsing](https://matklad.github.io/2020/04/13/simple-but-powerful-pratt-parsing.html) — names the subtler variants on this skeleton
