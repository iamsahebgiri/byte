#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "scanner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void repl() {
  printf("Byte v.0.1 \n");

  char line[1024];
  for (;;) {
    printf(">> ");

    if (!fgets(line, sizeof(line), stdin)) {
      printf("\n");
      break;
    }

    if (memcmp(line, "exit", 4) == 0) {
      break;
    }

    if (memcmp(line, "help", 4) == 0) {
      printf("exit - Exit the program\n");
    }

    interpret(line);
  }
}

static char* readFile(const char* path) {
  FILE* file = fopen(path, "rb");

  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    exit(74);
  }

  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);

  char* buffer = (char*)malloc(fileSize + 1);

  if (buffer == NULL) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
    exit(74);
  }

  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
  if (bytesRead < fileSize) {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(74);
  }

  buffer[bytesRead] = '\0';

  fclose(file);
  return buffer;
}

// static void runFile(const char* path) {
//   char* source = readFile(path);
//   InterpretResult result = interpret(source);
//   free(source);  // [owner]

//   if (result == INTERPRET_COMPILE_ERROR)
//     exit(65);
//   if (result == INTERPRET_RUNTIME_ERROR)
//     exit(70);
// }

int main(int argc, const char* argv[]) {
  Chunk chunk;
  initChunk(&chunk);
  writeChunk(&chunk, OP_RETURN, 12);
  writeChunk(&chunk, OP_RETURN, 12);
  writeChunk(&chunk, OP_RETURN, 12);
  writeChunk(&chunk, OP_RETURN, 12);
  int constant = addConstant(&chunk, 1.2);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);
  disassembleChunk(&chunk, "TEST");
  freeChunk(&chunk);

  //   if (argc == 1) {
  //     repl();
  //   } else if (argc == 2) {
  //     runFile(argv[1]);
  //   } else {
  //     fprintf(stderr, "Usage: byte <path>\n");
  //     exit(64);
  //   }
  return 0;
}