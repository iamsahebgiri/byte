# Debugging

The compiler and VM can emit debug output while running. Debug output is
**disabled by default** so that program output stays clean; it is controlled by
the `BYTE_DEBUG_TRACE` CMake option.

| Define                  | Enabled by          | Output                                     |
| ----------------------- | ------------------- | ------------------------------------------ |
| `DEBUG_PRINT_CODE`      | `BYTE_DEBUG_TRACE`  | disassembly of every chunk after compiling |
| `DEBUG_TRACE_EXECUTION` | `BYTE_DEBUG_TRACE`  | per-instruction stack trace while running  |

```bash
cmake -S . -B build -DBYTE_DEBUG_TRACE=ON && cmake --build build
```

## Example

The following program (see the chunk API walkthrough in `README.md`):

```c
initVM();
Chunk chunk;
initChunk(&chunk);
int constant = addConstant(&chunk, 1.2);
writeChunk(&chunk, OP_CONSTANT, 123);
writeChunk(&chunk, constant, 123);

constant = addConstant(&chunk, 3.4);
writeChunk(&chunk, OP_CONSTANT, 123);
writeChunk(&chunk, constant, 123);

writeChunk(&chunk, OP_ADD, 123);

constant = addConstant(&chunk, 5.6);
writeChunk(&chunk, OP_CONSTANT, 123);
writeChunk(&chunk, constant, 123);

writeChunk(&chunk, OP_DIVIDE, 123);
writeChunk(&chunk, OP_NEGATE, 123);

writeChunk(&chunk, OP_RETURN, 123);
interpret(&chunk);
freeVM();
```

disassembles to:

```
0000  123 OP_CONSTANT         0 '1.2'
          [ 1.2 ]
0002    | OP_CONSTANT         1 '3.4'
          [ 1.2 ][ 3.4 ]
0004    | OP_ADD
          [ 4.6 ]
0005    | OP_CONSTANT         2 '5.6'
          [ 4.6 ][ 5.6 ]
0007    | OP_DIVIDE
          [ 0.821429 ]
0008    | OP_NEGATE
          [ -0.821429 ]
0009    | OP_RETURN
-0.821429
```

## Reading the disassembly

Per line: **offset**, **line number of the source**, **opcode**, then the
operand. For each instruction the VM trace also prints the value stack below
it (`[ 1.2 ]`), which makes miscompiled stack effects easy to spot.

## Snapshot tests vs. debug output

The golden snapshots in `tests/golden/` were generated with debug output
off, so they contain only program output. If you are working with
`BYTE_DEBUG_TRACE=ON`, rerun tests against a build with the flag off
(the default configuration), otherwise the disassembly/trace noise breaks them.
