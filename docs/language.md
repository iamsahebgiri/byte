# Grammar

Language-level reference for the grammar implemented in the compiler
(`src/compiler/compiler.c`). The scanner produces `TOKEN_NEWLINE` for every
`\n`, which the parser treats as a statement separator.

## Statement forms

```
program     -> ( newline | declaration )* EOF
declaration -> letDecl | statement
statement   -> printStmt | exprStmt

letDecl     -> "let" IDENTIFIER ( "=" expression )? terminator
printStmt   -> "print" expression     terminator
exprStmt    -> expression             terminator
expression  -> equality ( "=" expression )?   # assignment, right-assoc

terminator  -> ";" | NEWLINE
newline     -> NEWLINE
```

## Statement termination

Statements end with `terminator()` in `src/compiler/compiler.c`:

- **Newline or `;`** both terminate a statement (`terminator`); either works for
  `let`, `print`, and expression statements.
- The final statement of a source may also end at **EOF** — no trailing
  newline needed for files or REPL input.
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

- Assignment compiles to `OP_SET_GLOBAL` (instead of the `OP_DEFINE_GLOBAL`
  that `let` emits).
- Assignment is **right-associative**: `a = b = 1` sets both to `1`.
- Assigning to an identifier that was never declared with `let` is caught at
  runtime: `Undefined variable 'a'.` (The VM deletes the side effect the
  `tableSet` made before raising the error.)
- Only a bare global identifier is a valid target; anything else
  (`a * b = 2`) is a compile error: `Invalid assignment target.`
- Compound assignments (`+=`, ...) are not yet parsed.

## Semantics

- Variables declared with `let` are always global.
- Global variables are late bound: resolution happens dynamically at runtime.
- A `let` without an initializer binds `nil`.
