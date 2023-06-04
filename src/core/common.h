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

#define DEBUG_PRINT_CODE
#define DEBUG_TRACE_EXECUTION

#define BYTE_COPYRIGHT "Copyright (c) 2023 Saheb Giri"

#endif