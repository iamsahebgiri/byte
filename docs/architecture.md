# Architecture

Byte is a single-pass bytecode compiler and stack VM, following the design of
[Crafting Interpreters](https://craftinginterpreters.com).

## Pipeline

```
source .byte file
       ↓
   Scanner      src/compiler/scanner.c    characters → tokens
       ↓
   Parser       src/compiler/compiler.c   tokens → bytecode (single pass)
       ↓
   Chunk        src/core/chunk.c          bytecode + constant pool
       ↓
    VM          src/compiler/vm.c         bytecode → values on a stack
```

The pipeline is deliberately simple — the compiler compiles sources eagerly
into a `Chunk` and hands it straight to the VM's dispatch loop; there is no AST
in memory at run time.

## Compiler internals

- The parser is a handwritten recursive descent for statements
  (`declaration`/`statement` in `src/compiler/compiler.c`) and a **Pratt
  parser** for expressions (`parsePrecedence` with a precedence/climbing rule
  table). See [pratt-parsing.md](pratt-parsing.md) for notes on how Pratt
  parsing works.
- `ParseRule` associates each token type with its prefix parse function
  (literal? variable? grouping?), its infix function, and its binding power
  (`PREC_*`). `parsePrecedence` keeps consuming tokens at higher precedence
  levels, which makes operators "larger precedence" naturally added.
- Each statement compiles immediately into the current `Chunk`; there is no
  AST. Constants (numbers, strings, identifiers) are stored in the chunk's
  constant pool and referenced by index.

## Core data structures

| Component     | Files                                   | Purpose                                             |
| ------------- | --------------------------------------- | --------------------------------------------------- |
| `Value`/`Obj` | `src/core/value.c`, `src/core/object.c` | tagged value and heap objects (interned strings)    |
| `Chunk`       | `src/core/chunk.c`                      | growable bytecode array + constants                 |
| `Table`       | `src/core/table.c`                      | linear-probe hash table (string interning, globals) |
| `Scanner`     | `src/compiler/scanner.c`                | on-demand tokenizer, tracks line numbers            |
| `Compiler`    | `src/compiler/compiler.c`               | single-pass statement parser + expression compiler  |
| `VM`          | `src/compiler/vm.c`                     | expression stack, dispatch loop                     |

## Debug output

The compiler and VM can emit disassembly/trace output while running — see
[debugging.md](debugging.md) for how to enable it and what it looks like.
