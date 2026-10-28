// Browser entry point for the Byte interpreter.
//
// This file replaces main.c when building for WebAssembly (see web/build.sh):
// instead of reading a file or a REPL line, the JavaScript side passes a
// NUL-terminated source string in and gets the CLI-style exit code back.
//
// Program output still goes through printf/fprintf. Emscripten forwards those
// streams to Module.print / Module.printErr, which web/worker.js relays to the
// output pane. stdout is line buffered, so each `print` arrives as one line.

#include <emscripten/emscripten.h>
#include <stdio.h>

#include "compiler/vm.h"
#include "core/common.h"

// A run can end mid-frame (runtime error) and leaves globals and heap objects
// behind, so every run starts from a clean interpreter. freeVM() also releases
// all objects the previous program allocated.
static void resetVM(void) {
  freeVM();
  initVM();
}

// Called once after the module is instantiated.
EMSCRIPTEN_KEEPALIVE
void byte_init(void) { resetVM(); }

// Runs `source` and returns the same status codes as the native CLI:
//
//   0   success
//   65  compile error
//   70  runtime error
EMSCRIPTEN_KEEPALIVE
int byte_run(const char* source) {
  if (source == NULL) return 65;

  resetVM();

  InterpretResult result = interpret(source);

  // Flush before JS reports "done" so the final line is never left buffered.
  fflush(stdout);
  fflush(stderr);

  resetVM();  // give the run's memory back between executions

  switch (result) {
    case INTERPRET_OK:
      return 0;
    case INTERPRET_COMPILE_ERROR:
      return 65;
    case INTERPRET_RUNTIME_ERROR:
      return 70;
  }
  return 70;
}
