# Byte

Byte is a small bytecode interpreter written in C: a single-pass compiler that
turns source into bytecode chunks, and a stack VM that executes them on an
expression stack.

```text
# tests/print.byte
print 1 + 2
# 3
```

Statements can be terminated by a newline or a `;`. See
[docs/language.md](docs/language.md) for the full grammar.

## Quickstart

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   # configure
cmake --build build                            # build

./build/byte tests/print.byte                  # run a program file
./build/byte                                   # start the REPL
```

## Tests

Language tests are `.byte` programs in `tests/`, verified against snapshots in
`tests/golden/` (each snapshot pins a test's exit code, output and errors).

```bash
./tests/run.sh                 # run the suite
./tests/run.sh --update        # regenerate snapshots after an intentional change
ctest --test-dir build         # same suite through CMake/CTest
```

See the header of `tests/run.sh` for details.

## Documentation

| Document                                     | Contents                             |
| ---------------------------------------------| ------------------------------------- |
| [Language & grammar](docs/language.md)       | statement forms, termination rules, semantics |
| [Architecture](docs/architecture.md)         | scanner → compiler → VM pipeline, Pratt expressions |
| [Debugging](docs/debugging.md)               | disassembly and VM trace output      |
| [Pratt parsing](docs/pratt-parsing.md)       | notes on how expression parsing works |
| [Closures & upvalues](docs/closures.md)      | compile-time capture tables and runtime boxes |
