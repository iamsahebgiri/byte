#ifndef BYTE_COMMON_H
#define BYTE_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_INTERPOLATION_NESTING 8

// Fixes vasprintf usage
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

// Turned on with: cmake -S . -B build -DBYTE_DEBUG_TRACE=ON
// Prints disassembly/trace info while compiling and running,
// which pollutes program output (e.g. the test snapshots).
#ifdef BYTE_DEBUG_TRACE
#define DEBUG_PRINT_CODE
#define DEBUG_TRACE_EXECUTION
#define DEBUG_STRESS_GC
#define DEBUG_LOG_GC
#endif

#define UINT8_COUNT (UINT8_MAX + 1)

#define BYTE_COPYRIGHT "Copyright (c) 2023 Saheb Giri"

#endif
