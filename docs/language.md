# Grammar

Language-level reference for the grammar implemented in the compiler
(`src/compiler/compiler.c`). The scanner produces `TOKEN_NEWLINE` for every
`\n`, which the parser treats as a statement separator.

## Statement forms

```
program     -> want( newline | declaration ), EOF
declaration -> letDecl | statement | newline
statement   -> printStmt | exprStmt

letDecl     -> "let" IDENTIFIER ( "=" expression )? terminator
printStmt   -> "print" expression       terminator
exprStmt    -> expression               terminator

terminator  -> ";" | NEWLINE
newline     -> NEWLINE
```

## Statement termination

- **Newline or `;`** both terminate a statement (`terminator`); either works for
  `let`, `print`, and expression statements.
- Semicolons allow multiple statements on one line:

  ```
  print 1 + 2
  let a = 3; print a
  ```

- Blank lines are skipped — `declaration()` consumes stray `NEWLINE` tokens,
  so programs may have any amount of vertical whitespace.

## Semantics

- Variables declared with `let` are always global.
- Global variables are late bound: resolution happens dynamically at runtime.
- A `let` without an initializer binds `nil`.
