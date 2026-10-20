# Grammar

Language-level reference for the grammar implemented in the compiler
(`src/compiler/compiler.c`). The scanner produces `TOKEN_NEWLINE` for every
`\n`, which the parser treats as a statement separator.

## Statement forms

```
program     -> ( newline | declaration )* EOF
declaration -> letDecl | funcDecl | statement
statement   -> printStmt | ifStmt | whileStmt | forStmt
             | returnStmt | block | exprStmt

letDecl     -> "let" IDENTIFIER ( "=" expression )? terminator
funcDecl    -> "func" IDENTIFIER "(" parameters? ")" block
parameters  -> IDENTIFIER ( "," IDENTIFIER )*
printStmt   -> "print" expression     terminator
returnStmt  -> "return" expression?   terminator
exprStmt    -> expression             terminator
block       -> "{" ( newline | declaration )* "}"

ifStmt      -> "if" expression statement ( "else" statement )?
whileStmt   -> "while" expression statement
forStmt     -> "for" forInit? ";" expression? ";" expression? statement
forInit     -> ( "let" )? IDENTIFIER ( "=" expression )?

expression  -> equality ( "=" expression )?   # assignment, right-assoc

terminator  -> ";" | NEWLINE | EOF | "}"   # "}" only ends a block's last statement
newline     -> NEWLINE
```

Control-flow conditions are written Go-style, without parentheses around
them; the `{` that starts the body ends the condition expression:

```
if age >= 21 {
    print "eligible"
} else {
    print "ineligible"
}

while i < 10 {
    i += 1
    print i
}

for i = 0; i < 10; i += 1 {
    print i
}
```

`else` may also start on the line after the closing `}`.

## `for` loops

`for` uses the Go header form `init; condition; post`:

- The initializer **implicitly declares** a variable scoped to the loop, so
  `let` is not needed (and normally omitted):

  ```
  for i = 0; i < 10; i += 1 {
      print i
  }
  ```

- A `let` in the initializer is still accepted, but redundant.
- Any of the three clauses may be omitted, e.g. `for ; i < 10; i += 1`.
- The loop variable is not visible after the loop.
- The header may span multiple lines.

## Functions and `return`

Functions are declared with `func`, a parameter list, and a block body:

```
func add(a, b) {
    return a + b
}

func greet() {
    print "hello"
}
```

`return` ends the current function:

- **Bare return** — `return` followed by a terminator returns `nil`:

  ```
  func find() {
      return
  }
  ```

- **Value return** — `return expression` returns that value:

  ```
  func double(n) {
      return n * 2
  }
  ```

- A value must start on the same line as `return`. A newline immediately after
  `return` is a bare return; the next line is a separate statement.
- If control reaches the end of a function without a `return`, the function
  returns `nil`.

Like every other statement, `return` uses `terminator()`:

| Form                     | Valid because                          |
| ------------------------ | -------------------------------------- |
| `return`                 | newline ends the statement             |
| `return;`                | `;` ends the statement                 |
| `return 42`              | newline ends the statement             |
| `return 42;`             | `;` ends the statement                 |
| `func f() { return 42 }` | `}` ends the last statement in a block |

> **Implementation status:** return syntax compiles to `OP_NIL`/value followed
> by `OP_RETURN`. The VM currently treats `OP_RETURN` as the end of execution;
> it does not yet pop the call frame and resume the caller.

## Statement termination

Statements end with `terminator()` in `src/compiler/compiler.c`:

- **Newline or `;`** both terminate a statement (`terminator`); either works for
  `let`, `print`, `return`, and expression statements.
- The final statement of a source may also end at **EOF** — no trailing
  newline needed for files or REPL input.
- The last statement in a block may end directly at **`}`**, so one-line
  bodies such as `func f() { return 42 }` are allowed.
- Semicolons allow multiple statements on one line:

  ```
  print 1 + 2
  let a = 3; print a
  ```

- Blank lines are skipped — `declaration()` consumes stray `NEWLINE` tokens,
  so programs may have any amount of vertical whitespace.

## Assignment

A plain assignment `IDENTIFIER = expression` is an expression statement and a
valid replacement for a bare `let` rebind:

```
let a = 10
a = 20
print a
# 20
```

- Assignment compiles to `OP_SET_LOCAL` for a local variable or
  `OP_SET_GLOBAL` for a global one (instead of the define opcode that `let`
  emits).
- Assignment is **right-associative**: `a = b = 1` sets both to `1`.
- Assigning to a global identifier that was never declared is caught at
  runtime: `Undefined variable 'a'.` (The VM deletes the side effect the
  `tableSet` made before raising the error.)
- Only a bare identifier is a valid target; anything else (`a * b = 2`) is a
  compile error: `Invalid assignment target.`

### Compound assignment

`+=`, `-=`, `*=` and `/=` are supported. `a += b` is compiled as
`a = a + b`; the same applies to local and global variables:

```
let a = 10
a += 5   # 15
a -= 3   # 12
a *= 2   # 24
a /= 4   # 6
```

## Logical operators

`and` and `or` short-circuit and return one of their operands:

- `a and b` evaluates `b` only when `a` is truthy.
- `a or b` evaluates `b` only when `a` is falsy.

`nil` and `false` are falsy; every other value is truthy. `!` performs logical
negation.

## Semantics

- A `let` at the top level declares a **global** variable; a `let` inside a
  block (or a `for` initializer) declares a **local** scoped to that block.
- Global variables are late bound: resolution happens dynamically at runtime.
- A `let` without an initializer binds `nil`.
- `if`/`while`/`for` bodies that use `{}` introduce a new scope. A `for`
  initializer is scoped to the loop.
