## Disassembler

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

Outputs to given below -

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
