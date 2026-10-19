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
PREC_ASSIGNMENT    =, +=, ...         (reserved, see Known gaps)
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

```c
static void parsePrecedence(Parser* parser, Precedence precedence) {
  advance(parser);                          // previous = a real token
  ParseFn prefixRule = getRule(parser->previous.type)->prefix;

  if (prefixRule == NULL) {
    errorAtCurrent(parser, "Expect expression.");
    return;
  }
  prefixRule(parser);                       // build one "atom"

  while (precedence <= getRule(parser->current.type)->precedence) {
    advance(parser);
    ParseFn infixRule = getRule(parser->previous.type)->infix;
    infixRule(parser);                      // extend the atom leftward
  }
}

static void expression(Parser* parser) {
  parsePrecedence(parser, PREC_ASSIGNMENT); // loosest tier = full expression
}
```

Two details worth noticing:

- `advance()` runs **first**. The engine works purely through `previous`
  (the token just consumed) and `current` (the lookahead), so each parse
  function assumes `previous` holds the token it was invoked for.
- The stopping condition `precedence <= getRule(current)->precedence` is
  checked against the **lookahead token**, not the operator just handled.
  That check _is_ the algorithm.

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

## Known gaps / roadmap

The `PREC_*` ladder exists, but several tiers have no parse functions wired
in yet (their `rules[]` entries are `NULL`), so those constructs are currently
rejected by the parser:

- **`PREC_ASSIGNMENT`** — `=` and compound assignments are not accepted; `let`
  is the only way to create a binding (see [language.md](language.md)).
  `TOKEN_EQUAL` currently maps to `PREC_NONE`. When adding assignment, give
  `=` the `PREC_ASSIGNMENT` row so the engine stops at it, and handle the
  assignment inside the identifier's prefix rule (`variable()` matching `=`
  and compiling the right-hand side with `expression()`). Since the RHS is
  parsed with a matching `match('=')` inside the function (not via the infix
  loop), `a = b = 1` nests right-associatively.
- **`PREC_OR` / `PREC_AND`** — `or`/`and` infix functions emitting
  short-circuit jumps.
- **`PREC_RANGE`** — `..` ranges for `for i in 1..10` loops.
- **`PREC_CALL`** — calls and member access (`.`).

Failing loudly (`Expect expression.`) is fine while the language is small,
but the ladder already sits in the order the grammar should grow into.

## Where to read more

- Vaughan Pratt, [Top Down Operator Precedence](https://tdop.sourceforge.net/)
- Bob Nystrom, [Compiling Expressions](https://craftinginterpreters.com/compiling-expressions.html) (Crafting Interpreters, Ch. 17)
- [Simple but Powerful Pratt Parsing](https://matklad.github.io/2020/04/13/simple-but-powerful-pratt-parsing.html) — names the subtler variants on this skeleton
